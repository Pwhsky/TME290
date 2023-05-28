# Build
FROM alpine:3.17 as builder
RUN apk update && \
    apk --no-cache add \
        ca-certificates \
        cmake \
        g++ \
        make \
        linux-headers 
RUN apk add libcluon --no-cache --repository \
      https://chrberger.github.io/libcluon/alpine/v3.13 --allow-untrusted


# Update the package manager and install required packages
RUN apk update && \
    apk add --no-cache build-base cmake git gtk+3.0-dev jpeg-dev libpng-dev tiff-dev zlib-dev

# Clone the OpenCV repository
RUN git clone https://github.com/opencv/opencv.git /opt/opencv

# Create a build directory and move into it
WORKDIR /opt/opencv/build

# Configure OpenCV using CMake
RUN cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=RELEASE -DBUILD_SHARED_LIBS=OFF -DWITH_GTK=ON -DWITH_JPEG=ON -DWITH_PNG=ON -DWITH_TIFF=ON -DWITH_ZLIB=ON

# Build and install OpenCV
RUN make -j4 && \
    make install

# Clean up unnecessary packages and files
RUN apk del build-base cmake git && \
    rm -rf /opt/opencv

# Set the working directory to /app
WORKDIR /app

# Add your application code and run your application


ADD . /opt/sources
WORKDIR /opt/sources
RUN mkdir /tmp/build && cd /tmp/build && \
    cmake /opt/sources && \
    make && cp kiwi-cone-detection /tmp

# Deploy
FROM alpine:3.17
RUN apk update && \
    apk --no-cache add \
        libstdc++
COPY --from=builder /tmp/kiwi-cone-detection /usr/bin
CMD ["/usr/bin/kiwi-cone-detection"]
