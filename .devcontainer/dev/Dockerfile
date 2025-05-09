FROM ubuntu:24.04 AS ubuntu

RUN apt update && apt upgrade

RUN apt update && apt install -y \
 g++ \
 cmake \
 cmake-curses-gui \
 binutils \
 build-essential \
 ninja-build \
 clang-tools-19 \
 clang-format-19 \
 clang-tidy-19 \
 git \ 
 gdb

RUN apt update && apt install -y \
 vim \
 less 

RUN ulimit -c unlimited

WORKDIR /app

ADD . /app