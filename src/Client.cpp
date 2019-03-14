/**
 * @file Client.cpp
 *
 * This module contains the implementation of the Smtp::Client class.
 *
 * © 2019 by Richard Walters
 */

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
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

}

namespace Smtp {

    void Client::Extension::Configure(const std::string& parameters) {
    }

    std::string Client::Extension::ModifyMessage(
        const MessageContext& context,
        const std::string& input
    ) {
        return input;
    }

    bool Client::Extension::IsExtraProtocolStageNeededHere(
        const MessageContext& context
    ) {
        return false;
    }

    void Client::Extension::GoAhead(
        std::function< void(const std::string& data) > onSendMessage,
        std::function< void(bool success) > onStageComplete
    ) {
    }

    bool Client::Extension::HandleServerMessage(
        const MessageContext& context,
        const ParsedMessage& message
    ) {
        return false;
    }

    /**
     * This contains the private properties of a Client instance.
     */
    struct Client::Impl
        : public std::enable_shared_from_this< Impl >
    {
        // Types

        /**
         * This is the type of collection which holds onto promises made
         * to be completed when either the SMTP client and server are ready
         * to process the next message, or the connection has been broken.
         *
         * The value set in the promise will be false if the connection
         * has been broken.
         */
        using ReadyOrBrokenPromises = std::vector< std::promise< bool > >;

        // Properties

        /**
         * This is a helper object used to generate and publish
         * diagnostic messages.
         */
        SystemAbstractions::DiagnosticsSender diagnosticsSender;

        /**
         * This is used to protect the other properties of this structure
         * when accessed simultaneously by multiple threads.
         */
        std::mutex mutex;

        /**
         * These are the SMTP extensions registered for use by the client.
         */
        std::map< std::string, std::shared_ptr< Extension > > extensions;

        /**
         * These are the names of the SMTP extensions that the server supports
         * and that the client has registered.
         */
        std::set< std::string > supportedExtensionNames;

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
         * This holds any promises made to publish the fact that the SMTP
         * client and server are both ready to handle the next message,
         * or that the connection has been broken.
         */
        ReadyOrBrokenPromises readyOrBrokenPromises;

        /**
         * This is set when the SMTP client is finished sending an e-mail.
         */
        std::promise< bool > sendCompleted;

        /**
         * This holds any data received from the server, before that data
         * has been chopped up into lines.
         */
        std::vector< uint8_t > dataReceived;

        /**
         * This holds any information that needs to be shared between the
         * protocol handler and any extensions.
         */
        MessageContext currentMessageContext;

        /**
         * If this is not nullptr, it points to the SMTP extension which is
         * currently talking to the SMTP server.  In essence, the extension
         * is runnings its own "protocol stage".
         */
        std::shared_ptr< Extension > activeExtension;

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

        // Methods

        /**
         * This is the default constructor of the structure
         */
        Impl()
            : diagnosticsSender("Smtp")
        {
        }

        /**
         * Take the current ready-or-broken promises and return them, placing
         * an empty collection in its place.
         *
         * @return
         *     The ready-or-broken promises that were previously registered
         *     are returned.
         */
        ReadyOrBrokenPromises SwapOutReadyOrBrokenPromises() {
            std::lock_guard< decltype(mutex) > lock(mutex);
            ReadyOrBrokenPromises promisesReturned;
            promisesReturned.swap(readyOrBrokenPromises);
            return promisesReturned;
        }

        /**
         * Handle a failure in communication with the SMTP server.
         */
        void OnHardFailure() {
            auto promises = SwapOutReadyOrBrokenPromises();
            for (auto& promise: promises) {
                promise.set_value(false);
            }
            serverConnection->Close();
        }

        /**
         * Handle the condition where the SMTP client and server are both ready
         * to process the next message.
         */
        void OnReady() {
            auto promises = SwapOutReadyOrBrokenPromises();
            for (auto& promise: promises) {
                promise.set_value(true);
            }
        }

        /**
         * Move the client the next stage in the SMTP protocol.  Give supported
         * extensions the opportunity to run their own stages if they want.
         *
         * @param[in] nextProtocolStage
         *     This is the next stage in the SMTP protocol.
         */
        void TransitionProtocolStage(Client::ProtocolStage nextProtocolStage) {
            activeExtension = nullptr;
            currentMessageContext.protocolStage = nextProtocolStage;
            for (const auto& supportedExtensionName: supportedExtensionNames) {
                const auto extension = extensions[supportedExtensionName];
                if (
                    extension->IsExtraProtocolStageNeededHere(
                        currentMessageContext
                    )
                ) {
                    activeExtension = extension;
                    activeExtension->GoAhead(
                        std::bind(&Impl::SendMessageDirectly, this, std::placeholders::_1),
                        std::bind(&Impl::OnExtensionStageComplete, this, std::placeholders::_1)
                    );
                    break;
                }
            }
            if (
                (currentMessageContext.protocolStage == Client::ProtocolStage::ReadyToSend)
                && (activeExtension == nullptr)
            ) {
                OnReady();
            }
        }

        /**
         * Handle a message ready in communication with the SMTP server.
         */
        void OnMessageReady() {
            TransitionProtocolStage(Client::ProtocolStage::ReadyToSend);
        }

        /**
         * React to the failure to send an e-mail through the SMTP server,
         * where the connection is still kept alive and the client can
         * attempt another transaction if it wants to.
         */
        void OnSoftFailure() {
            sendCompleted.set_value(false);
            OnMessageReady();
        }

        /**
         * Move on to the next protocol stage, or complete the transaction
         * with a failure.
         *
         * @param[in] success
         *     This indicates whether or not the transaction can proceed
         *     to the next stage.
         */
        void OnExtensionStageComplete(bool success) {
            if (success) {
                TransitionProtocolStage(currentMessageContext.protocolStage);
            } else {
                OnSoftFailure();
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
            std::vector< Client::ParsedMessage >& parsedMessages
        ) {
            for (const auto& line: lines) {
                diagnosticsSender.SendDiagnosticInformationString(
                    0,
                    "S: " + line.substr(0, line.length() - 2)
                );
                Client::ParsedMessage parsedMessage;
                if (
                    (line.length() < 4)
                    || (sscanf(line.c_str(), "%d", &parsedMessage.code) != 1)
                ) {
                    OnHardFailure();
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
                        OnHardFailure();
                        return false;
                    }
                }
                parsedMessage.text = line.substr(4, line.length() - 6);
                parsedMessages.push_back(std::move(parsedMessage));
            }
            return true;
        }

        /**
         * Send the given message to the SMTP server without processing it
         * with any extensions.
         *
         * @param[in] message
         *     This is the message to send.  Each line of the message should
         *     have a newline at the end.
         */
        void SendMessageDirectly(const std::string& message) {
            diagnosticsSender.SendDiagnosticInformationString(
                0,
                "C: " + message.substr(0, message.length() - 2)
            );
            serverConnection->SendMessage(
                std::vector< uint8_t >(
                    message.begin(),
                    message.end()
                )
            );
        }

        /**
         * Process the given message through all supported and registered
         * extensions, and then send it to the SMTP server.
         *
         * @note
         *     A newline is added to the processed message before it's sent.
         *
         * @param[in] input
         *     This is the message to be processed and then sent.  It does not
         *     have a newline at the end.
         */
        void SendMessageThroughExtensions(const std::string& input) {
            std::string output(input);
            for (const auto& supportedExtensionName: supportedExtensionNames) {
                output = extensions[supportedExtensionName]->ModifyMessage(
                    currentMessageContext,
                    output
                );
            }
            SendMessageDirectly(output + "\r\n");
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
                if (activeExtension) {
                    if (
                        activeExtension->HandleServerMessage(
                            currentMessageContext,
                            parsedMessage
                        )
                    ) {
                        continue;
                    } else {
                        OnHardFailure();
                        return;
                    }
                }
                switch (currentMessageContext.protocolStage) {
                    case ProtocolStage::Greeting: {
                        if (parsedMessage.code == 220) {
                            SendMessageDirectly("EHLO alex.example.com\r\n");
                            TransitionProtocolStage(ProtocolStage::Options);
                        } else {
                            OnHardFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::HelloResponse: {
                        if (parsedMessage.code == 250) {
                            if (parsedMessage.last) {
                                OnMessageReady();
                            } else {
                                TransitionProtocolStage(ProtocolStage::Options);
                            }
                        } else {
                            OnHardFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::Options: {
                        if (parsedMessage.code == 250) {
                            auto delimiter = parsedMessage.text.find(' ');
                            decltype(delimiter) parametersStart = delimiter + 1;
                            if (delimiter == std::string::npos) {
                                delimiter = parsedMessage.text.length();
                                parametersStart = delimiter;
                            }
                            const auto supportedExtensionName = parsedMessage.text.substr(
                                0,
                                delimiter
                            );
                            auto extensionsEntry = extensions.find(supportedExtensionName);
                            if (extensionsEntry != extensions.end()) {
                                (void)supportedExtensionNames.insert(supportedExtensionName);
                                extensionsEntry->second->Configure(
                                    parsedMessage.text.substr(parametersStart)
                                );
                            }
                            if (parsedMessage.last) {
                                OnMessageReady();
                            }
                        } else {
                            OnHardFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::DeclaringSender: {
                        if (parsedMessage.code == 250) {
                            TransitionProtocolStage(ProtocolStage::DeclaringRecipients);
                            for (const auto& recipient: headers.GetHeaderMultiValue("To")) {
                                recipients.push(recipient);
                            }
                            AnnounceNextRecipient();
                        } else {
                            OnSoftFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::DeclaringRecipients: {
                        if (parsedMessage.code == 250) {
                            if (recipients.empty()) {
                                SendMessageThroughExtensions("DATA");
                                TransitionProtocolStage(ProtocolStage::SendingData);
                            } else {
                                AnnounceNextRecipient();
                            }
                        } else {
                            OnSoftFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::SendingData: {
                        if (parsedMessage.code == 354) {
                            TransitionProtocolStage(ProtocolStage::AwaitingSendResponse);
                            SendMessageDirectly(headers.GenerateRawHeaders());
                            SendMessageDirectly(body);
                            SendMessageDirectly(".\r\n");
                        } else {
                            OnSoftFailure();
                            return;
                        }
                    } break;

                    case ProtocolStage::AwaitingSendResponse: {
                        sendCompleted.set_value(parsedMessage.code == 250);
                        OnMessageReady();
                    } break;

                    default: {
                        OnHardFailure();
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
            OnHardFailure();
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
                tls->SubscribeToDiagnostics(diagnosticsSender.Chain(), 1);
                tls->ConfigureAsClient(
                    serverConnection,
                    caCerts,
                    serverHostName
                );
                serverConnection = tls;
            } else {
                serverConnection->SubscribeToDiagnostics(diagnosticsSender.Chain(), 1);
            }
            const auto hostAddress = SystemAbstractions::NetworkConnection::GetAddressOfHost(
                serverHostName
            );
            if (hostAddress == 0) {
                diagnosticsSender.SendDiagnosticInformationString(
                    SystemAbstractions::DiagnosticsSender::Levels::WARNING,
                    "Unable to determine IP address of SMTP server"
                );
                return false;
            }
            if (!serverConnection->Connect(hostAddress, serverPortNumber)) {
                diagnosticsSender.SendDiagnosticInformationString(
                    SystemAbstractions::DiagnosticsSender::Levels::WARNING,
                    "Unable to connect to SMTP server"
                );
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
            SendMessageThroughExtensions(
                SystemAbstractions::sprintf(
                    "RCPT TO:<%s>",
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

    SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate Client::SubscribeToDiagnostics(
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
        size_t minLevel
    ) {
        return impl_->diagnosticsSender.SubscribeToDiagnostics(delegate, minLevel);
    }

    void Client::EnableTls(const std::string& caCerts) {
        impl_->useTls = true;
        impl_->caCerts = caCerts;
    }

    void Client::RegisterExtension(
        const std::string& extensionName,
        std::shared_ptr< Extension > extensionImplementation
    ) {
        impl_->extensions[extensionName] = extensionImplementation;
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
            (impl_->currentMessageContext.protocolStage == ProtocolStage::ReadyToSend)
            && (headers.HasHeader("From"))
        ) {
            impl_->headers = headers;
            impl_->body = body;
            impl_->SendMessageThroughExtensions(
                SystemAbstractions::sprintf(
                    "MAIL FROM:<%s>",
                    headers.GetHeaderValue("From").c_str()
                )
            );
            impl_->TransitionProtocolStage(Client::ProtocolStage::DeclaringSender);
        } else {
            impl_->sendCompleted.set_value(false);
        }
        return impl_->sendCompleted.get_future();
    }

    std::future< bool > Client::GetReadyOrBrokenFuture() {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->readyOrBrokenPromises.push_back(std::promise< bool >());
        auto& newReadyOrBrokenPromise = impl_->readyOrBrokenPromises.back();
        return newReadyOrBrokenPromise.get_future();
    }

}
