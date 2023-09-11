FROM ubuntu:jammy

USER root

RUN apt-get update -y && \
    apt-get install -y git libglib2.0-dev libfdt-dev libpixman-1-dev ninja-build && \
    apt-get install -y python-pip python3-venv && \
    apt-get install -y flex bison  && \ 
    apt-get install -y python3 && \
    apt-get install -y python3-venv && \
    apt-get install -y python3-pip && \
    apt-get install -y libsdl2-2.0 && \
    apt-get install -y libsndio-dev

COPY ./ /src/

WORKDIR /src

SHELL ["/bin/bash", "-c"]

RUN mkdir -p build
WORKDIR /src/build
RUN ../configure --target-list=sparc-softmmu --enable-debug
# RUN make

# copy over whatever.exe to this container and then execute
#   qemu-system-sparc -no-reboot -nographic -M leon3_generic -m 64M -kernel whatever.exe
CMD ["tail", "-f", "/dev/null"]