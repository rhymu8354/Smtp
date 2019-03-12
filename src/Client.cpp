/**
 * @file Client.cpp
 *
 * This module contains the implementation of the Smtp::Client class.
 *
 * © 2019 by Richard Walters
 */

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <Smtp/Client.hpp>
#include <stddef.h>
#include <stdio.h>
#include <SystemAbstractions/NetworkConnection.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <thread>
#include <TlsDecorator/TlsDecorator.hpp>
#include <vector>

namespace {

    /**
     */
    constexpr std::chrono::milliseconds TLS_HANDSHAKE_TIMEOUT = std::chrono::milliseconds(1000);

    /**
     * This is used to keep track of the progression of the SMTP protocol.
     */
    enum class ProtocolStage {
        /**
         * In this stage, the client is waiting for the server greeting.
         */
        Greeting,

        /**
         * In this stage, the client is waiting for the server to finish
         * providing all the options it supports.
         */
        Options,

        /**
         * In this stage, the client is ready to send the next message.
         */
        ReadyToSend,

        /**
         * In this stage, the client is waiting for the server to accept
         * the sender address.
         */
        DeclaringSender,

        /**
         * In this stage, the client is waiting for the server to accept
         * the recipient addresses.
         */
        DeclaringRecipients,

        /**
         * In this stage, the client is waiting for the server to give
         * the go-ahead to receive the message headers and body.
         */
        SendingData,

        /**
         * In this stage, the client is waiting for the server to give
         * the final response about sending the e-mail.
         */
        AwaitingSendResponse,
    };

    /**
     * This is used to hold onto the pieces of a disassembled message
     * received from an SMTP server.
     */
    struct ParsedMessage {
        /**
         * This is the 3-digit code provided by the server that gives
         * the program a general indication of the server/protocol status.
         */
        int code = 0;

        /**
         * This indicates whether or not the server indicated that this is
         * the last line it will send in the current protocol stage.
         */
        bool last = false;

        /**
         * This is a human-readable string provided with the message that
         * can be delivered to the user to explain what's going on.
         */
        std::string text;
    };

}

namespace Smtp {

    /**
     * This contains the private properties of a Client instance.
     */
    struct Client::Impl
        : public std::enable_shared_from_this< Impl >
    {
        // Types

        using EventPromises = std::vector< std::promise< void > >;

        // Properties

        /**
         * This is used to protect the other properties of this structure
         * when accessed simultaneously by multiple threads.
         */
        std::mutex mutex;

        /**
         * This is the interface to the next layer down in protocols
         * (either the TLS layer or the TCP layer, depending on whether
         * or not TLS was enabled).
         */
        std::shared_ptr< SystemAbstractions::INetworkConnection > serverConnection;

        /**
         * This indicates whether or not to include a Transport Layer Security
         * (TLS) layer between the application (SMTP) and network (TCP) layers.
         */
        bool useTls = false;

        /**
         * This is the concatenation of the root Certificate Authority
         * (CA) certificates to trust, in PEM format.
         */
        std::string caCerts;

        /**
         * This holds any promises made to publish any failure event.
         */
        EventPromises failurePromises;

        /**
         * This holds any promises made to publish the fact that the SMTP
         * client and server are both ready to handle the next message.
         */
        EventPromises messageReadyPromises;

        /**
         * This is set when the SMTP client is finished sending an e-mail.
         */
        std::promise< bool > sendCompleted;

        /**
         * This tracks the progress of the communication with the SMTP server.
         */
        ProtocolStage protocolStage = ProtocolStage::Greeting;

        /**
         * This holds any data received from the server, before that data
         * has been chopped up into lines.
         */
        std::vector< uint8_t > dataReceived;

        /**
         * This is a copy of the headers for the e-mail currently being sent.
         */
        MessageHeaders::MessageHeaders headers;

        /**
         * This is a copy of the body of the e-mail currently being sent.
         */
        std::string body;

        /**
         * This holds the e-mail addresses of the recipients of the e-mail
         * currently being sent, that have not yet been given to the server.
         */
        std::queue< std::string > recipients;

        /**
         * Take the current failure promises and return them, placing
         * an empty collection in its place.
         *
         * @return
         *     The failure promises that were previously registered
         *     are returned.
         */
        EventPromises SwapOutFailurePromises() {
            std::lock_guard< decltype(mutex) > lock(mutex);
            EventPromises promisesReturned;
            promisesReturned.swap(failurePromises);
            return promisesReturned;
        }

        /**
         * Handle a failure in communication with the SMTP server.
         */
        void OnFailure() {
            auto promises = SwapOutFailurePromises();
            for (auto& promise: promises) {
                promise.set_value();
            }
            serverConnection->Close();
        }

        /**
         * Take the current message ready promises and return them, placing
         * an empty collection in its place.
         *
         * @return
         *     The message ready promises that were previously registered
         *     are returned.
         */
        EventPromises SwapOutMessageReadyPromises() {
            std::lock_guard< decltype(mutex) > lock(mutex);
            EventPromises promisesReturned;
            promisesReturned.swap(messageReadyPromises);
            return promisesReturned;
        }

        /**
         * Handle a message ready in communication with the SMTP server.
         */
        void OnMessageReady() {
            protocolStage = ProtocolStage::ReadyToSend;
            auto promises = SwapOutMessageReadyPromises();
            for (auto& promise: promises) {
                promise.set_value();
            }
        }

        /**
         * Append the given data to the reassembly buffer.
         * Then extract and return any completed lines of text.
         *
         * @param[in] data
         *     This is the sequence of raw bytes received from the server.
         *
         * @return
         *     A collection of completed lines of text reassembled from
         *     the data received now and previously is returned.
         */
        std::vector< std::string > AssembleLinesReceived(
            const std::vector< uint8_t >& data
        ) {
            std::vector< std::string > linesReceived;
            dataReceived.insert(
                dataReceived.end(),
                data.begin(),
                data.end()
            );
            while (!dataReceived.empty()) {
                auto cr = std::find(
                    dataReceived.begin(),
                    dataReceived.end(),
                    '\r'
                );
                if (
                    (cr == dataReceived.end())
                    || (cr + 1 == dataReceived.end())
                    || (*(cr + 1) != '\n')
                ) {
                    break;
                }
                linesReceived.push_back(
                    std::string(
                        dataReceived.begin(),
                        cr + 2
                    )
                );
                (void)dataReceived.erase(
                    dataReceived.begin(),
                    cr + 2
                );
            }
            return linesReceived;
        }

        /**
         * Break up the given lines of text received from the SMTP server,
         * and detect if there is any problem.
         *
         * @param[in] lines
         *     These are the lines of text received from the SMTP server.
         *
         * @param[out] parsedMessages
         *     This is where to store the disassembled messages.
         *
         * @return
         *     An indication of whether or not a problem was detected
         *     is returned.
         *
         * @note
         *     If a problem was detected, a failure event will be published,
         *     and the connection to the server closed.
         */
        bool DisassembleMessagesReceived(
            const std::vector< std::string >& lines,
            std::vector< ParsedMessage >& parsedMessages
        ) {
            for (const auto& line: lines) {
                ParsedMessage parsedMessage;
                if (
                    (line.length() < 4)
                    || (sscanf(line.c_str(), "%d", &parsedMessage.code) != 1)
                ) {
                    OnFailure();
                    return false;
                }
                switch (line[3]) {
                    case '-': {
                        parsedMessage.last = false;
                        break;
                    }

                    case ' ': {
                        parsedMessage.last = true;
                        break;
                    }

                    default: {
                        OnFailure();
                        return false;
                    }
                }
                parsedMessage.text = line.substr(4, line.length() - 6);
                parsedMessages.push_back(std::move(parsedMessage));
            }
            return true;
        }

        /**
         * Send the given message to the SMTP server.
         *
         * @param[in] message
         *     This is the message to send.
         */
        void SendMessage(const std::string& message) {
            serverConnection->SendMessage(
                std::vector< uint8_t >(
                    message.begin(),
                    message.end()
                )
            );
        }

        /**
         * Handle the receipt of raw bytes from the underlying transport
         * layer.
         *
         * @param[in] message
         *     This holds the raw bytes received from the transport layer.
         */
        void OnMessageReceived(const std::vector< uint8_t >& message) {
            const auto lines = AssembleLinesReceived(message);
            if (lines.empty()) {
                return;
            }
            std::vector< ParsedMessage > parsedMessages;
            if (!DisassembleMessagesReceived(lines, parsedMessages)) {
                return;
            }
            for (const auto& parsedMessage: parsedMessages) {
                switch (protocolStage) {
                    case ProtocolStage::Greeting: {
                        if (parsedMessage.code == 220) {
                            SendMessage("EHLO alex.example.com\r\n");
                            protocolStage = ProtocolStage::Options;
                        } else {
                            OnFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::Options: {
                        if (parsedMessage.code == 250) {
                            if (parsedMessage.last) {
                                OnMessageReady();
                            }
                        } else {
                            OnFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::DeclaringSender: {
                        if (parsedMessage.code == 250) {
                            protocolStage = ProtocolStage::DeclaringRecipients;
                            for (const auto& recipient: headers.GetHeaderMultiValue("To")) {
                                recipients.push(recipient);
                            }
                            AnnounceNextRecipient();
                        } else {
                            sendCompleted.set_value(false);
                            OnMessageReady();
                            return;
                        }
                    } break;

                    case ProtocolStage::DeclaringRecipients: {
                        if (parsedMessage.code == 250) {
                            protocolStage = ProtocolStage::DeclaringRecipients;
                            if (recipients.empty()) {
                                SendMessage("DATA\r\n");
                                protocolStage = ProtocolStage::SendingData;
                            } else {
                                AnnounceNextRecipient();
                            }
                        } else {
                            sendCompleted.set_value(false);
                            OnMessageReady();
                            return;
                        }
                    } break;

                    case ProtocolStage::SendingData: {
                        if (parsedMessage.code == 354) {
                            protocolStage = ProtocolStage::AwaitingSendResponse;
                            SendMessage(headers.GenerateRawHeaders());
                            SendMessage(body);
                            SendMessage(".\r\n");
                        } else {
                            sendCompleted.set_value(false);
                            OnMessageReady();
                            return;
                        }
                    } break;

                    case ProtocolStage::AwaitingSendResponse: {
                        sendCompleted.set_value(parsedMessage.code == 250);
                        OnMessageReady();
                    } break;

                    default: {
                        OnFailure();
                        return;
                    }
                }
            }
        }

        /**
         * Handle the publishing of the event that the underlying transport
         * layer was closed.
         *
         * @param[in] graceful
         *     This indicates whether or not the connection was closed
         *     without being reset by the peer.
         */
        void OnBroken(bool graceful) {
            OnFailure();
        }

        /**
         * Synchronously initiate a connection to an SMTP server.
         *
         * @param[in] clientHostName
         *     This is the name to advertise to the server as being
         *     the host name or literal address of the client.
         *
         * @param[in] serverHostName
         *     This is the name of the SMTP server's host.
         *
         * @param[in] serverPortNumber
         *     This is the TCP port number of the SMTP server.
         *
         * @return
         *     An indication of whether or not the connection to the SMTP
         *     server was successfully made is returned.
         */
        bool Connect(
            const std::string& clientHostName,
            const std::string& serverHostName,
            const uint16_t serverPortNumber
        ) {
            serverConnection = std::make_shared< SystemAbstractions::NetworkConnection >();
            std::shared_ptr < TlsDecorator::TlsDecorator > tls;
            if (useTls) {
                tls = std::make_shared< TlsDecorator::TlsDecorator >();
                tls->ConfigureAsClient(
                    serverConnection,
                    caCerts,
                    serverHostName
                );
                serverConnection = tls;
            }
            const auto hostAddress = SystemAbstractions::NetworkConnection::GetAddressOfHost(
                serverHostName
            );
            if (hostAddress == 0) {
                return false;
            }
            if (!serverConnection->Connect(hostAddress, serverPortNumber)) {
                return false;
            }
            std::weak_ptr< Impl > selfWeak(shared_from_this());
            const auto messageReceivedDelegate = [selfWeak](
                const std::vector< uint8_t >& message
            ){
                auto self = selfWeak.lock();
                if (self == nullptr) {
                    return;
                }
                self->OnMessageReceived(message);
            };
            const auto brokenDelegate = [selfWeak](
                bool graceful
            ){
                auto self = selfWeak.lock();
                if (self == nullptr) {
                    return;
                }
                self->OnBroken(graceful);
            };
            if (
                !serverConnection->Process(
                    messageReceivedDelegate,
                    brokenDelegate
                )
            ) {
                return false;
            }
            if (useTls) {
                auto handshakeCompleted = std::make_shared< std::promise< void > >();
                auto handshakeWasCompleted = handshakeCompleted->get_future();
                tls->SetHandshakeCompleteDelegate(
                    [handshakeCompleted](
                        const std::string& certificate
                    ){
                        handshakeCompleted->set_value();
                    }
                );
                if (
                    handshakeWasCompleted.wait_for(TLS_HANDSHAKE_TIMEOUT)
                    != std::future_status::ready
                ) {
                    return false;
                }
            }
            return true;
        }

        /**
         * Send the next recipient e-mail address to the SMTP server.
         */
        void AnnounceNextRecipient() {
            const auto nextRecipient = recipients.front();
            recipients.pop();
            SendMessage(
                SystemAbstractions::sprintf(
                    "RCPT TO:<%s>\r\n",
                    nextRecipient.c_str()
                )
            );
        }
    };

    Client::~Client() noexcept = default;
    Client::Client(Client&& other) noexcept = default;
    Client& Client::operator=(Client&& other) noexcept = default;

    Client::Client()
        : impl_(new Impl)
    {
    }

    void Client::EnableTls(const std::string& caCerts) {
        impl_->useTls = true;
        impl_->caCerts = caCerts;
    }

    std::future< bool > Client::Connect(
        const std::string& clientHostName,
        const std::string& serverHostName,
        const uint16_t serverPortNumber
    ) {
        auto impl(impl_);
        return std::async(
            [
                impl,
                serverHostName,
                serverPortNumber,
                clientHostName
            ]{
                return impl->Connect(
                    clientHostName,
                    serverHostName,
                    serverPortNumber
                );
            }
        );
    }

    std::future< bool > Client::SendMail(
        const MessageHeaders::MessageHeaders& headers,
        const std::string& body
    ) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->sendCompleted = std::promise< bool >();
        if (
            (impl_->protocolStage == ProtocolStage::ReadyToSend)
            && (headers.HasHeader("From"))
        ) {
            impl_->headers = headers;
            impl_->body = body;
            impl_->SendMessage(
                SystemAbstractions::sprintf(
                    "MAIL FROM:<%s>\r\n",
                    headers.GetHeaderValue("From").c_str()
                )
            );
            impl_->protocolStage = ProtocolStage::DeclaringSender;
        } else {
            impl_->sendCompleted.set_value(false);
        }
        return impl_->sendCompleted.get_future();
    }

    std::future< void > Client::GetFailureFuture() {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->failurePromises.push_back(std::promise< void >());
        auto& newFailurePromise = impl_->failurePromises.back();
        return newFailurePromise.get_future();
    }

    std::future< void > Client::GetMessageReadyBeSentFuture() {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->messageReadyPromises.push_back(std::promise< void >());
        auto& newMessageReadyPromise = impl_->messageReadyPromises.back();
        return newMessageReadyPromise.get_future();
    }

}
