FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
	build-essential \
	libncurses-dev \
	bison \
	flex \
	python3 \
	libssl-dev \
	libelf-dev \
	bc \
	git \
	rsync \
	cpio \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /kernel-src
