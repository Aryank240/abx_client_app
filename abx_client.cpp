#include <iostream>
#include <boost/asio>
#include <vector>
#include <nlohmann/json.hpp>
#include <fstream>

using boost::asio::ip::tcp;
using json = nlohmann::json;

// Constants
const std::string SERVER_HOST = "127.0.0.1";  // Server host
const int SERVER_PORT = 3000;                // Server port

// Function to send a request to the server
void sendRequest(tcp::socket& socket, uint8_t callType, uint8_t resendSeq = 0) {
    std::vector<uint8_t> request = {callType, resendSeq};
    boost::asio::write(socket, boost::asio::buffer(request));
}

// Function to parse the server response
void parseResponse(std::vector<uint8_t>& responseBuffer, json& outputJson) {
    const size_t packetSize = 17;  // Each packet is 17 bytes

    for (size_t i = 0; i < responseBuffer.size(); i += packetSize) {
        if (i + packetSize > responseBuffer.size()) break;

        std::string symbol(responseBuffer.begin() + i, responseBuffer.begin() + i + 4);
        char buySell = responseBuffer[i + 4];
        int32_t quantity = ntohl(*reinterpret_cast<int32_t*>(&responseBuffer[i + 5]));
        int32_t price = ntohl(*reinterpret_cast<int32_t*>(&responseBuffer[i + 9]));
        int32_t sequence = ntohl(*reinterpret_cast<int32_t*>(&responseBuffer[i + 13]));

        // Add parsed packet to JSON array
        outputJson.push_back({
            {"symbol", symbol},
            {"buySell", std::string(1, buySell)},
            {"quantity", quantity},
            {"price", price},
            {"sequence", sequence}
        });
    }
}

// Main function
int main() {
    try {
        // Step 1: Initialize Boost.Asio
        boost::asio::io_context ioContext;
        tcp::socket socket(ioContext);

        // Step 2: Connect to the server
        tcp::resolver resolver(ioContext);
        boost::asio::connect(socket, resolver.resolve(SERVER_HOST, std::to_string(SERVER_PORT)));

        // Step 3: Send "Stream All Packets" request
        sendRequest(socket, 1);

        // Step 4: Receive response
        std::vector<uint8_t> responseBuffer(1024);  // Adjust size as needed
        size_t bytesRead = socket.read_some(boost::asio::buffer(responseBuffer));

        // Step 5: Parse response and identify missing sequences
        responseBuffer.resize(bytesRead);  // Trim unused buffer
        json outputJson = json::array();
        parseResponse(responseBuffer, outputJson);

        // Identify missing sequences
        std::vector<int32_t> sequences;
        for (const auto& packet : outputJson) {
            sequences.push_back(packet["sequence"]);
        }

        std::sort(sequences.begin(), sequences.end());
        for (size_t i = 1; i < sequences.size(); ++i) {
            if (sequences[i] != sequences[i - 1] + 1) {
                // Missing sequence detected
                int32_t missingSeq = sequences[i - 1] + 1;
                std::cout << "Requesting missing sequence: " << missingSeq << std::endl;

                sendRequest(socket, 2, static_cast<uint8_t>(missingSeq));

                // Receive and parse the missing packet
                bytesRead = socket.read_some(boost::asio::buffer(responseBuffer));
                responseBuffer.resize(bytesRead);
                parseResponse(responseBuffer, outputJson);
            }
        }

        // Step 6: Save output JSON to file
        std::ofstream outputFile("output.json");
        outputFile << outputJson.dump(4);  // Pretty-print JSON with 4 spaces
        outputFile.close();

        std::cout << "Output JSON saved to 'output.json'." << std::endl;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
    }

    return 0;
}
