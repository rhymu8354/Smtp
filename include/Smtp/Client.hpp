#pragma once

/**
 * @file Client.hpp
 *
 * This module declares the Smtp::Client class.
 *
 * © 2019 by Richard Walters
 */

#include <functional>
#include <future>
#include <memory>
#include <MessageHeaders/MessageHeaders.hpp>
#include <string>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <SystemAbstractions/INetworkConnection.hpp>

namespace Smtp {

    /**
     * This class implements the client portion of the Simple Mail Transport
     * Protocol (SMTP -- [RFC 5321](https://tools.ietf.org/html/rfc5321).
     */
    class Client {
        // Types
    public:
        /**
         * This is used to keep track of the progression of the SMTP protocol.
         */
        enum class ProtocolStage {
            /**
             * In this stage, the client is waiting for the server greeting.
             */
            Greeting,

            /**
             * In this stage, the client is waiting for the server to respond to
             * the client's EHLO.
             */
            HelloResponse,

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

        /**
         * Forward-declare the SMTP extension class so that MessageContext can
         * use it.
         */
        class Extension;

        /**
         * This holds any information that needs to be shared between the
         * protocol handler and any extensions.
         */
        struct MessageContext {
            /**
             * This tracks the progress of the communication with the SMTP
             * server.
             */
            ProtocolStage protocolStage = ProtocolStage::Greeting;
        };

        /**
         * This is the interface any extensions need to implement in order to
         * plug into this protocol handler.
         */
        class Extension {
            // Methods
        public:
            /**
             * Set up the extension to be used in communicating with an
             * SMTP server.
             *
             * @param[in] parameters
             *     This contains any text included by the SMTP server when
             *     this extension was listed as supported.
             */
            virtual void Configure(const std::string& parameters);

            /**
             * Reset the extension for use in a new message transaction.
             */
            virtual void Reset();

            /**
             * Allow the extension to modify the given message about to be sent
             * from the client to the server.
             *
             * @param[in] context
             *     This holds any information that needs to be shared between
             *     the protocol handler and the extension.
             *
             * @param[in] input
             *     This is the message about to the sent from the client
             *     to the server.  The newline at the end is not included.
             *
             * @return
             *     The input message, possibly modified by the extension,
             *     is returned.  The newline at the end is not included.
             */
            virtual std::string ModifyMessage(
                const MessageContext& context,
                const std::string& input
            );

            /**
             * Ask the extension whether or not it wants to handle a custom
             * protocol step at the current time.
             *
             * @param[in] context
             *     This holds any information that needs to be shared between
             *     the protocol handler and the extension.
             *
             * @return
             *     An indication of whether or not the extension wants to
             *     handle a custom protocol step at the current time
             *     is returned.
             */
            virtual bool IsExtraProtocolStageNeededHere(
                const MessageContext& context
            );

            /**
             * Tell the extension that it should proceed in its custom
             * protocol stage.
             *
             * @param[in] onSendMessage
             *     This is a function the extension can call to send
             *     data directly to the SMTP server.
             *
             * @param[in] onSoftFailure
             *     This is a function the extension can call to let the
             *     SMTP client know that the current message failed to be
             *     sent, and the protocol should go back to the "ready to
             *     send" stage.
             *
             * @param[in] onStageComplete
             *     This is a function the extension can call to let the
             *     SMTP client know that the custom procotol stage is
             *     complete.  The parameter indicates whether or not the
             *     client may proceed to the next stage.
             *
             */
            virtual void GoAhead(
                std::function< void(const std::string& data) > onSendMessage,
                std::function< void(bool success) > onStageComplete
            );

            /**
             * Give the extension the opportunity to modify a message that
             * is about to be sent to the SMTP server.
             *
             * @param[in] context
             *     This holds any information that needs to be shared between
             *     the protocol handler and the extension.
             *
             * @param[in] message
             *     This is the message the extension is being asked to modify.
             *
             * @return
             *     An indication of whether or not the message was handled
             *     successfully is returned.
             *
             * @retval false
             *     If this is returned, the client should consider the
             *     protocol to have encountered a "hard failure", meaning
             *     an unrecoverable error where the connection should
             *     be dropped.
             */
            virtual bool HandleServerMessage(
                const MessageContext& context,
                const ParsedMessage& message
            );
        };

        /**
         * This is the interface to the dependency of the class which
         * implements network connections the class needs to communicate with
         * SMTP servers.
         */
        class Transport {
        public:
            /**
             * Establish a new connection to a server.
             *
             * @param[in] hostNameOrAddress
             *     This is the host name or IPv4 address of the server to
             *     which to connect.
             *
             * @param[in] port
             *     This is the port number of the server.
             *
             * @return
             *     An object used to communicate with the server is returned.
             *
             * @retval nullptr
             *     This is returned if a connection to the server could not
             *     be established.
             */
            virtual std::shared_ptr< SystemAbstractions::INetworkConnection > Connect(
                const std::string& hostNameOrAddress,
                uint16_t port
            ) = 0;
        };

        // Lifecycle management
    public:
        ~Client() noexcept;
        Client(const Client&) = delete;
        Client(Client&&) noexcept;
        Client& operator=(const Client&) = delete;
        Client& operator=(Client&&) noexcept;

        // Public methods
    public:
        /**
         * This is the default constructor.
         */
        Client();

        /**
         * This method forms a new subscription to diagnostic
         * messages published by the class.
         *
         * @param[in] delegate
         *     This is the function to call to deliver messages
         *     to the subscriber.
         *
         * @param[in] minLevel
         *     This is the minimum level of message that this subscriber
         *     desires to receive.
         *
         * @return
         *     A function is returned which may be called
         *     to terminate the subscription.
         */
        SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate SubscribeToDiagnostics(
            SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
            size_t minLevel = 0
        );

        /**
         * Provide the dependencies the class needs to operate.
         *
         * @param[in] transport
         *     This is the object used to make network connections to
         *     SMTP servers.
         */
        void Configure(std::shared_ptr< Transport > transport);

        /**
         * Provide the implementation of an SMTP extension to be used (if the
         * server supports it) in any subsequent connection.
         *
         * @param[in] extensionName
         *     This is the name used by the SMTP server to identify the
         *     extension.
         *
         * @param[in] extensionImplementation
         *     This is the object which implements the extension being
         *     registered.
         */
        void RegisterExtension(
            const std::string& extensionName,
            std::shared_ptr< Extension > extensionImplementation
        );

        /**
         * Asynchronously initiate a connection to an SMTP server.
         *
         * @param[in] serverHostName
         *     This is the name of the SMTP server's host.
         *
         * @param[in] serverPortNumber
         *     This is the TCP port number of the SMTP server.
         *
         * @return
         *     A future is returned that is set when the connection
         *     process is completed.
         */
        std::future< bool > Connect(
            const std::string& serverHostName,
            const uint16_t serverPortNumber
        );

        /**
         * Close any open connection to the SMTP server.
         */
        void Disconnect();

        /**
         * Asynchronously initiate the sending of an e-mail through
         * the SMTP server.
         *
         * @note
         *     The client must be connected first.  Use the Connect
         *     method and wait for the returned future to be ready
         *     before attempting to call this method.
         *
         * @param[in] headers
         *     These are the headers to use for the message to send.
         *
         * @param[in] body
         *     This is the body of the message to send.
         *
         * @return
         *     A future is returned that is set when the e-mail has
         *     either been received or rejected by the server.  The value
         *     relayed through the future indicates whether or not the
         *     e-mail was received successfully.
         */
        std::future< bool > SendMail(
            const MessageHeaders::MessageHeaders& headers,
            const std::string& body
        );

        /**
         * Return a future that is set once the SMTP client and server
         * are ready to process the next message, or the connection is
         * broken.
         *
         * @return
         *     A future that is set once the SMTP client and server
         *     are ready to process the next message, or the connection
         *     is broken, is returned.  The value returned from the future
         *     is false in the case of the connection being broken.
         */
        std::future< bool > GetReadyOrBrokenFuture();

        // Private properties
    private:
        /**
         * This is the type of structure that contains the private
         * properties of the instance.  It is defined in the implementation
         * and declared here to ensure that it is scoped inside the class.
         */
        struct Impl;

        /**
         * This contains the private properties of the instance.
         */
        std::shared_ptr< Impl > impl_;
    };

}
