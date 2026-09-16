cmake_minimum_required(VERSION 3.10)
project(schedule_bot_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(TgBot REQUIRED)

add_executable(bot main.cpp)

target_link_libraries(bot 
    TgBot::TgBot
    ${CURL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    pthread
)