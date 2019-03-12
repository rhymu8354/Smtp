#pragma once

/**
 * @file Client.hpp
 *
 * This module declares the Smtp::Client class.
 *
 * © 2019 by Richard Walters
 */

#include <future>
#include <memory>
#include <MessageHeaders/MessageHeaders.hpp>
#include <string>

namespace Smtp {

    /**
     * This class implements the client portion of the Simple Mail Transport
     * Protocol (SMTP -- [RFC 5321](https://tools.ietf.org/html/rfc5321).
     */
    class Client {
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
         * Set the instance up to include a Transport Layer Security (TLS)
         * layer between the application (SMTP) and network (TCP) layers.
         *
         * @param[in] caCerts
         *     This is the concatenation of the root Certificate Authority
         *     (CA) certificates to trust, in PEM format.
         */
        void EnableTls(const std::string& caCerts);

        /**
         * Asynchronously initiate a connection to an SMTP server.
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
         *     A future is returned that is set when the connection
         *     process is completed.
         */
        std::future< bool > Connect(
            const std::string& clientHostName,
            const std::string& serverHostName,
            const uint16_t serverPortNumber
        );

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
         * Return a future that is set if any exchange with the SMTP
         * server fails.
         *
         * @return
         *     A future that is set if any exchange with the SMTP
         *     server fails is returned.
         */
        std::future< void > GetFailureFuture();

        /**
         * Return a future that is set once the SMTP client and server
         * are ready to process the next message.
         *
         * @return
         *     A future that is set once the SMTP client and server
         *     are ready to process the next message is returned.
         */
        std::future< void > GetMessageReadyBeSentFuture();

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
