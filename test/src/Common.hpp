/**
 * @file Common.hpp
 *
 * This module declares a common unit test fixture shared between the various
 * test modules for SMTP.
 *
 * © 2019 by Richard Walters
 */

#include <algorithm>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <MessageHeaders/MessageHeaders.hpp>
#include <Smtp/Client.hpp>
#include <stdint.h>
#include <string>
#include <SystemAbstractions/NetworkEndpoint.hpp>
#include <TlsDecorator/TlsDecorator.hpp>
#include <vector>

namespace SmtpTests {

    extern const std::string testGoodCertificate;
    extern const std::string testBadCertificate;
    extern const std::string testKey;

    template< typename T > bool FutureReady(
        std::future< T >& future,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(0)
    ) {
        return (
            future.wait_for(timeout)
            == std::future_status::ready
        );
    }

    /**
     * This holds information about one client that is connected
     * to the server used in the text fixture for these tests.
     */
    struct Client {
        /**
         * This is the server end of the connection between the unit under
         * test and the server.
         */
        std::shared_ptr< SystemAbstractions::INetworkConnection > connection;

        /**
         * This holds any data received from the client, before that data
         * has been chopped up into lines.
         */
        std::vector< uint8_t > dataReceived;

        /**
         * This holds any text lines extracted from the data received from the
         * client.
         */
        std::vector< std::string > linesReceived;

        /**
         * This flag indicates whether or not the connection to the client
         * was broken by the client.
         */
        bool broken = false;
    };

    struct SmtpTransport
        : public Smtp::Client::Transport
    {
        bool useTls = false;
        std::string caCerts;
        std::shared_ptr< SystemAbstractions::INetworkConnection > lastServerConnection;

        // Smtp::Client::Transport

        virtual std::shared_ptr< SystemAbstractions::INetworkConnection > Connect(
            const std::string& hostNameOrAddress,
            uint16_t port
        ) override;
    };

    /**
     * This is the base for test fixtures used to test the SMTP library.
     */
    struct Common
        : public ::testing::Test
    {
        // Properties

        /**
         * This is the unit under test.
         */
        Smtp::Client client;

        std::shared_ptr< SmtpTransport > transport = std::make_shared< SmtpTransport >();

        /**
         * This is a real network server used to test that the unit under test
         * can actually connect to a real server.
         */
        SystemAbstractions::NetworkEndpoint server;

        /**
         * This is the TCP port number selected by the test server.
         */
        uint16_t serverPort = 0;

        /**
         * This collects information about any connections
         * established (presumably by the unit under test) to the server.
         */
        std::vector< Client > clients;

        /**
         * This is used to synchronize access to the object.
         */
        std::mutex mutex;

        /**
         * This is used to wake up threads which may be waiting for some
         * state in the fixture to be changed.
         */
        std::condition_variable_any waitCondition;

        /**
         * This method waits for the given number of connections to be established
         * with the server.
         *
         * @param[in] numConnections
         *     This is the number of connections to await.
         *
         * @return
         *     An indication of whether or not the given number of connections
         *     were established with the server before a reasonable amount of
         *     time has elapsed is returned.
         */
        bool AwaitConnections(size_t numConnections);

        std::vector< std::string > AwaitMessages(
            size_t clientIndex,
            size_t numMessages,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)
        );

        bool AwaitBroken(
            size_t clientIndex,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)
        );

        bool EstablishConnection(bool useTls);

        bool EstablishConnectionPrepareToSend(bool verifyMessageReadyToBeSent = true);

        std::future< bool > StartSendingEmail();

        void SendTextMessage(
            SystemAbstractions::INetworkConnection& connection,
            const std::string& message
        );

        void StartServer(bool useTls);

        // ::testing::Test

        virtual void SetUp() override;
        virtual void TearDown() override;
    };

}
