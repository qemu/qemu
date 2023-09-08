FROM ubuntu:18.04

USER root

RUN apt-get update -y && \
    apt-get install -y git libglib2.0-dev libfdt-dev libpixman-1-dev ninja-build && \
    apt-get install -y python-pip python3-venv && \
    apt-get install -y flex bison  && \ 
    apt-get install -y python3.7 && \
    apt-get install -y python3.7-venv && \
    apt-get install -y python3-pip

COPY ./ /src/

WORKDIR /src

SHELL ["/bin/bash", "-c"]

RUN update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.7 1

RUN mkdir -p build
WORKDIR /src/build
RUN ../configure --target-list=sparc-softmmu --enable-debug
# RUN make


# copy over whatever.exe to this container and then execute
#   qemu-system-sparc -no-reboot -nographic -M leon3_generic -m 64M -kernel whatever.exe
CMD ["tail", "-f", "/dev/null"]