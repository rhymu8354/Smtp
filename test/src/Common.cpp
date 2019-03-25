/**
 * @file Common.cpp
 *
 * This module contains the implementation of a common unit test fixture shared
 * between the various test modules for SMTP.
 *
 * © 2019 by Richard Walters
 */

#include "Common.hpp"

#include <algorithm>
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

    std::shared_ptr< SystemAbstractions::INetworkConnection > SmtpTransport::Connect(
        const std::string& hostNameOrAddress,
        uint16_t port
    ) {
        std::shared_ptr< SystemAbstractions::INetworkConnection > serverConnection
            = std::make_shared< SystemAbstractions::NetworkConnection >();
        std::shared_ptr < TlsDecorator::TlsDecorator > tls;
        if (useTls) {
            tls = std::make_shared< TlsDecorator::TlsDecorator >();
            tls->ConfigureAsClient(
                serverConnection,
                caCerts,
                hostNameOrAddress
            );
            serverConnection = tls;
        }
        const auto hostAddress = SystemAbstractions::NetworkConnection::GetAddressOfHost(
            hostNameOrAddress
        );
        if (hostAddress == 0) {
            return nullptr;
        }
        if (!serverConnection->Connect(hostAddress, port)) {
            return nullptr;
        }
        lastServerConnection = serverConnection;
        return serverConnection;
    }

    const std::string testGoodCertificate = (
        "-----BEGIN CERTIFICATE-----\r\n"
        "MIIEpDCCAowCCQCuHs5BKOVHazANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAls\r\n"
        "b2NhbGhvc3QwHhcNMTgxMjIxMDAxODMyWhcNMTkxMjIxMDAxODMyWjAUMRIwEAYD\r\n"
        "VQQDDAlsb2NhbGhvc3QwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQDH\r\n"
        "PLIotLZCPCEvqjXvFwlp0knFVKieOe+OaSQ1hNaRU0m6TdD6sQ3wldF0nGsRQN1c\r\n"
        "LZRfwmYCo4uzDYC3UyrEriim62DvX0I1xhLROvHUnkoEEGfKFQ8Djl5G1dLnPfwk\r\n"
        "3ULCZ/2jGZOTO+HNQRHnm3hL+yXvuk8vIkWrNd31IkqzZp0y8vI4M3HWLL1XUt3Q\r\n"
        "hzOyb8XzVgrnDIBJbgOgE4VaXt5HEZatqYG5Uhfv1NAosh1FUa9b823PQPOVkh3m\r\n"
        "5w1NYV4PZ4SZUa5VoP+w675bBCNm0OCHPqpP2LGjSo7t6jfPXHwm5OMfbXCNxBH4\r\n"
        "9ZkekOS+hOPqLfIWEwzZ92lXHDCrYkH/OZwJaja1zNc8BS9ojAKE1hVRSCd44EpL\r\n"
        "GpGxQsuPWonb/b+RgrwWigI3G6eE2xcVj5CSxKNiKQGcQy8QVnjGgTJBPelhFewN\r\n"
        "tx1Flt/b4LhvUDqTBCtU7KppJfSO0bEnfiJ5z5tpJpgt/rPnClB0R2uxuCpj9pVf\r\n"
        "yEEbv1Y7/JnLvH7Llpwukekwdkz7hGrunl/lK0hwQPi1z1EiesH3wxDqqBBL+zgz\r\n"
        "kuTRZioGhjvMT8mCwtr/vmjUEep66JsJNh7Fo/wKRIxKF/fzgmiOIeXoMsW+LljD\r\n"
        "yzctBXJpQhB8iHYUg9Hpv0vMzS9lX6gz9/U3UuuHCwIDAQABMA0GCSqGSIb3DQEB\r\n"
        "CwUAA4ICAQDAfR8V1BVoBMqc4U+6OdagV3REVZafd9Rzf8PjSaIWeiM9wa/4h+dc\r\n"
        "C86bX9KUk9bV/iyYL9mSbudhFCIDWITPgPiDFL70c/WCleYj3XpE6GCS1T1B0OOM\r\n"
        "y/3EE++v+PHAPYrKTCwHNxsIEUWJA2aRUqj+tIaFuFdHCpU/KSbyuoWgDvYdEdkQ\r\n"
        "+sOCbpIxNx4Je2++2RxZ03mu9UZNAVlGKJeTe3sAdFSfwAvVv6/lgIL+GUt9o4A7\r\n"
        "FFA6ggp0rxaB4BDL7aNwWeVtuPjF68m7oEcjOFfOxI7fqTmD+W+5omplGDYucO7x\r\n"
        "IQFvWaSWYm1PyoPex6TvJaWGXnuaAWme8bZUyE4/YmdpZZMUQ1YUZDhZYaHMFjkE\r\n"
        "P+ia7c3MYYvpfOTr5gThCaU08lsUjcOoJvItDdy/G++GB8YECHA7LMwYCjBBFqVW\r\n"
        "RaQJuMKqhfPglNjf5my+uoitlyd2DOYJ2Kec23vFHU3zjHFChrc58YGgoVz/q9wk\r\n"
        "JQun12/8OAAEVnvA5AQONXvti/P95Xv0qLijHSCMPErNECYeZOdM2VSHzfnBc4rC\r\n"
        "svb7P+PdQ5JAAqjQwKWR/iKImLU+a/n5b0tb/oTabyqaz2pBAPcs79yf1uKXBSy9\r\n"
        "kRB/XrrxB8HRF+3Nu9jLcDdI3AZR1NWZrYAXabRJkq/nwQy5CCQ3ZQ==\r\n"
        "-----END CERTIFICATE-----\r\n"
    );
    const std::string testBadCertificate = (
        "-----BEGIN CERTIFICATE-----\r\n"
        "NIIEpDCCAowCCQCuHs5BKOVHazANBgkqhkiG9w0BAQsFADAUMRIwEAYDVQQDDAls\r\n"
        "b2NhbGhvc3QwHhcNMTgxMjIxMDAxODMyWhcNMTkxMjIxMDAxODMyWjAUMRIwEAYD\r\n"
        "VQQDDAlsb2NhbGhvc3QwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQDH\r\n"
        "PLIotLZCPCEvqjXvFwlp0knFVKieOe+OaSQ1hNaRU0m6TdD6sQ3wldF0nGsRQN1c\r\n"
        "LZRfwmYCo4uzDYC3UyrEriim62DvX0I1xhLROvHUnkoEEGfKFQ8Djl5G1dLnPfwk\r\n"
        "3ULCZ/2jGZOTO+HNQRHnm3hL+yXvuk8vIkWrNd31IkqzZp0y8vI4M3HWLL1XUt3Q\r\n"
        "hzOyb8XzVgrnDIBJbgOgE4VaXt5HEZatqYG5Uhfv1NAosh1FUa9b823PQPOVkh3m\r\n"
        "5w1NYV4PZ4SZUa5VoP+w675bBCNm0OCHPqpP2LGjSo7t6jfPXHwm5OMfbXCNxBH4\r\n"
        "9ZkekOS+hOPqLfIWEwzZ92lXHDCrYkH/OZwJaja1zNc8BS9ojAKE1hVRSCd44EpL\r\n"
        "GpGxQsuPWonb/b+RgrwWigI3G6eE2xcVj5CSxKNiKQGcQy8QVnjGgTJBPelhFewN\r\n"
        "tx1Flt/b4LhvUDqTBCtU7KppJfSO0bEnfiJ5z5tpJpgt/rPnClB0R2uxuCpj9pVf\r\n"
        "yEEbv1Y7/JnLvH7Llpwukekwdkz7hGrunl/lK0hwQPi1z1EiesH3wxDqqBBL+zgz\r\n"
        "kuTRZioGhjvMT8mCwtr/vmjUEep66JsJNh7Fo/wKRIxKF/fzgmiOIeXoMsW+LljD\r\n"
        "yzctBXJpQhB8iHYUg9Hpv0vMzS9lX6gz9/U3UuuHCwIDAQABMA0GCSqGSIb3DQEB\r\n"
        "CwUAA4ICAQDAfR8V1BVoBMqc4U+6OdagV3REVZafd9Rzf8PjSaIWeiM9wa/4h+dc\r\n"
        "C86bX9KUk9bV/iyYL9mSbudhFCIDWITPgPiDFL70c/WCleYj3XpE6GCS1T1B0OOM\r\n"
        "y/3EE++v+PHAPYrKTCwHNxsIEUWJA2aRUqj+tIaFuFdHCpU/KSbyuoWgDvYdEdkQ\r\n"
        "+sOCbpIxNx4Je2++2RxZ03mu9UZNAVlGKJeTe3sAdFSfwAvVv6/lgIL+GUt9o4A7\r\n"
        "FFA6ggp0rxaB4BDL7aNwWeVtuPjF68m7oEcjOFfOxI7fqTmD+W+5omplGDYucO7x\r\n"
        "IQFvWaSWYm1PyoPex6TvJaWGXnuaAWme8bZUyE4/YmdpZZMUQ1YUZDhZYaHMFjkE\r\n"
        "P+ia7c3MYYvpfOTr5gThCaU08lsUjcOoJvItDdy/G++GB8YECHA7LMwYCjBBFqVW\r\n"
        "RaQJuMKqhfPglNjf5my+uoitlyd2DOYJ2Kec23vFHU3zjHFChrc58YGgoVz/q9wk\r\n"
        "JQun12/8OAAEVnvA5AQONXvti/P95Xv0qLijHSCMPErNECYeZOdM2VSHzfnBc4rC\r\n"
        "svb7P+PdQ5JAAqjQwKWR/iKImLU+a/n5b0tb/oTabyqaz2pBAPcs79yf1uKXBSy9\r\n"
        "kRB/XrrxB8HRF+3Nu9jLcDdI3AZR1NWZrYAXabRJkq/nwQy5CCQ3ZQ==\r\n"
        "-----END CERTIFICATE-----\r\n"
    );
    const std::string testKey = (
        "-----BEGIN PRIVATE KEY-----\r\n"
        "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\r\n"
        "MF64udQeuah9lvj+C3NvY2tyL2bh8nE=\r\n"
        "-----END PRIVATE KEY-----\r\n"
    );

    bool Common::AwaitConnections(size_t numConnections) {
        std::unique_lock< std::mutex > lock(mutex);
        return waitCondition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this, numConnections]{
                return (clients.size() >= numConnections);
            }
        );
    }

    std::vector< std::string > Common::AwaitMessages(
        size_t clientIndex,
        size_t numMessages,
        std::chrono::milliseconds timeout
    ) {
        std::unique_lock< std::mutex > lock(mutex);
        auto& client = clients[clientIndex];
        (void)waitCondition.wait_for(
            lock,
            timeout,
            [this, clientIndex, numMessages]{
                auto& client = clients[clientIndex];
                return (client.linesReceived.size() >= numMessages);
            }
        );
        decltype(client.linesReceived) messagesReceived;
        messagesReceived.swap(client.linesReceived);
        return messagesReceived;
    }

    bool Common::AwaitBroken(
        size_t clientIndex,
        std::chrono::milliseconds timeout
    ) {
        std::unique_lock< std::mutex > lock(mutex);
        auto& client = clients[clientIndex];
        return waitCondition.wait_for(
            lock,
            timeout,
            [this, clientIndex]{
                auto& client = clients[clientIndex];
                return client.broken;
            }
        );
    }

    bool Common::EstablishConnection(bool useTls) {
        if (useTls) {
            transport->useTls = true;
            transport->caCerts = testGoodCertificate;
        }
        auto connectionDidComplete = client.Connect(
            "localhost",
            serverPort
        );
        if (
            connectionDidComplete.wait_for(std::chrono::milliseconds(1000))
            != std::future_status::ready
        ) {
            return false;
        }
        if (!connectionDidComplete.get()) {
            return false;
        }
        return AwaitConnections(1);
    }

    bool Common::EstablishConnectionPrepareToSend(bool verifyMessageReadyToBeSent) {
        StartServer(false);
        if (!EstablishConnection(false)) {
            return false;
        }
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(
            connection,
            "220 mail.example.com Simple Mail Transfer Service Ready\r\n"
        );
        if (AwaitMessages(0, 1).size() != 1) {
            return false;
        }
        SendTextMessage(connection, "250-mail.example.com\r\n");
        SendTextMessage(connection, "250-FOO Poggers\r\n");
        SendTextMessage(connection, "250 BAR\r\n");
        if (verifyMessageReadyToBeSent) {
            if (!FutureReady(readyOrBroken, std::chrono::milliseconds(1000))) {
                return false;
            }
            return readyOrBroken.get();
        }
        return true;
    }

    std::future< bool > Common::StartSendingEmail() {
        std::promise< bool > sendCompletedEarly;
        auto sendWasCompletedEarly = sendCompletedEarly.get_future();
        if (!EstablishConnectionPrepareToSend()) {
            sendCompletedEarly.set_value(false);
            return sendWasCompletedEarly;
        }
        MessageHeaders::MessageHeaders headers;
        headers.AddHeader("From", "<alex@example.com>");
        headers.AddHeader("To", "<bob@example.com>");
        headers.AddHeader("To", "<carol@example.com>");
        headers.AddHeader("Subject", "food.exe");
        const std::string body = (
            "Have you heard of food.exe?  admEJ\r\n"
            "That was a great game!\r\n"
        );
        auto sendWasCompleted = client.SendMail(headers, body);
        if (AwaitMessages(0, 1).size() != 1) {
            sendCompletedEarly.set_value(false);
            return sendWasCompletedEarly;
        }
        return sendWasCompleted;
    }

    void Common::SendTextMessage(
        SystemAbstractions::INetworkConnection& connection,
        const std::string& message
    ) {
        connection.SendMessage(
            std::vector< uint8_t >(
                message.begin(),
                message.end()
            )
        );
    }

    void Common::StartServer(bool useTls) {
        const auto newConnectionDelegate = [this, useTls](
            std::shared_ptr< SystemAbstractions::NetworkConnection > newConnection
        ){
            std::shared_ptr< SystemAbstractions::INetworkConnection > newConnectionInterface(newConnection);
            if (useTls) {
                const auto tls = std::make_shared< TlsDecorator::TlsDecorator >();
                tls->ConfigureAsServer(
                    newConnectionInterface,
                    testGoodCertificate,
                    testKey,
                    ""
                );
                newConnectionInterface = tls;
            }
            std::unique_lock< decltype(mutex) > lock(mutex);
            size_t connectionIndex = clients.size();
            if (
                newConnectionInterface->Process(
                    [this, connectionIndex](const std::vector< uint8_t >& data){
                        std::unique_lock< decltype(mutex) > lock(mutex);
                        auto& dataReceived = clients[connectionIndex].dataReceived;
                        auto& linesReceived = clients[connectionIndex].linesReceived;
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
                        waitCondition.notify_all();
                    },
                    [this, connectionIndex](bool graceful){
                        std::unique_lock< decltype(mutex) > lock(mutex);
                        auto& broken = clients[connectionIndex].broken;
                        broken = true;
                        waitCondition.notify_all();
                    }
                )
            ) {
                Client newClient;
                newClient.connection = newConnectionInterface;
                clients.push_back(std::move(newClient));
                waitCondition.notify_all();
            }
        };
        const auto packetReceivedDelegate = [](
            uint32_t address,
            uint16_t port,
            const std::vector< uint8_t >& body
        ){
        };
        ASSERT_TRUE(
            server.Open(
                newConnectionDelegate,
                packetReceivedDelegate,
                SystemAbstractions::NetworkEndpoint::Mode::Connection,
                0x7F000001,
                0,
                0
            )
        );
        serverPort = server.GetBoundPort();
    }

    void Common::SetUp() {
        client.Configure(transport);
    }

    void Common::TearDown() {
        server.Close();
        clients.clear();
    }

}
