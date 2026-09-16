FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y g++ cmake make git libcurl4-openssl-dev libssl-dev zlib1g-dev libboost-system-dev libboost-program-options-dev nlohmann-json3-dev
RUN git clone https://github.com/reo7sp/tgbot-cpp.git /tgbot-cpp
WORKDIR /tgbot-cpp
RUN cmake . && make -j1 && make install
WORKDIR /app
COPY . .
RUN cmake . && make -j1

FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y libcurl4 libssl3 zlib1g libboost-system1.74.0 libboost-program-options1.74.0 && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/bot /usr/local/bin/bot
EXPOSE 8080
CMD ["bot"]