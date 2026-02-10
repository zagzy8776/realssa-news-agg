FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    libcurl4-openssl-dev \
    libssl-dev \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Download cpp-httplib header
WORKDIR /app
RUN wget https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h

# Copy source code
COPY realssa_news_linux.cpp .
COPY realssa_news_linux.cpp .
# Compile
RUN g++ -std=c++17 -pthread -o realssa_news realssa_news_linux.cpp -lcurl -lssl -lcrypto

# Expose port
EXPOSE 3000

# Run
CMD ["./realssa_news"]
