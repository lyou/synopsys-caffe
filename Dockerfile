FROM ubuntu:18.04

# dummy to force refresh binder service to re-build docker image.
RUN ls -all

# Environment variables and args

ARG NOTEBOOK_USER=root
ARG NOTEBOOK_UID=1000
ENV USER ${NOTEBOOK_USER}
ENV NOTEBOOK_UID ${NOTEBOOK_UID}
ENV HOME /home/${NOTEBOOK_USER}

WORKDIR ${HOME}

USER root
# Downloads the package lists from the repositories and "updates" them 
# to get information on the newest versions of packages and their dependencies.
RUN apt-get update

# Install 'curl': Command line tool that allows you to transfer data from or to a remote server. 
# With curl, you can download or upload data using HTTP, HTTPS, SCP, SFTP, and FTP.
RUN apt-get install -y curl



# Install .NET CLI dependencies
RUN apt-get install -y --no-install-recommends \
        libc6 \
        libgcc1 \
        libgssapi-krb5-2 \
        libicu60 \
        libssl1.1 \
        libstdc++6 \
        zlib1g 

RUN rm -rf /var/lib/apt/lists/*

# Install .NET Core SDK
ENV DOTNET_SDK_VERSION 3.0.100

RUN curl -SL --output dotnet.tar.gz https://dotnetcli.blob.core.windows.net/dotnet/Sdk/$DOTNET_SDK_VERSION/dotnet-sdk-$DOTNET_SDK_VERSION-linux-x64.tar.gz \
    && dotnet_sha512='766da31f9a0bcfbf0f12c91ea68354eb509ac2111879d55b656f19299c6ea1c005d31460dac7c2a4ef82b3edfea30232c82ba301fb52c0ff268d3e3a1b73d8f7' \
    && echo "$dotnet_sha512 dotnet.tar.gz" | sha512sum -c - \
    && mkdir -p /usr/share/dotnet \
    && tar -zxf dotnet.tar.gz -C /usr/share/dotnet \
    && rm dotnet.tar.gz \
    && ln -s /usr/share/dotnet/dotnet /usr/bin/dotnet

# Enable detection of running in a container
ENV DOTNET_RUNNING_IN_CONTAINER=true \
    # Enable correct mode for dotnet watch (only mode supported in a container)
    DOTNET_USE_POLLING_FILE_WATCHER=true \
    # Skip extraction of XML docs - generally not useful within an image/container - helps performance
    NUGET_XMLDOC_MODE=skip

# Trigger first run experience by running arbitrary cmd
RUN dotnet help

# Building image START

# workaround bug https://grigorkh.medium.com/fix-tzdata-hangs-docker-image-build-cdb52cc3360d
ENV TZ=Asia/Dubai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt update
RUN apt install -y tzdata


RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        wget \
        python3.8-dev \
        python3-skimage \
        python3-opencv \
        python3-pip \
        #required by pandas
        libgfortran5 \
        libopenblas-dev \
        libatlas-base-dev \
        libboost-python1.65-dev \
        libboost-all-dev \
        libgflags-dev \
        #Directly incorporate Google glog projects from Github instead of consume it.
        #See https://github.com/google/glog#incorporating-glog-into-a-cmake-project
        #Not install google glog into env and incorporate into cmake build directly.
        libgoogle-glog-dev \
        libhdf5-serial-dev \
        libleveldb-dev \
        liblmdb-dev \
        libopencv-dev \
        libprotobuf-dev \
        libsnappy-dev \
        libmatio-dev \
        protobuf-compiler && \
    rm -rf /var/lib/apt/lists/*

ENV CAFFE_ROOT=/opt/caffe
WORKDIR $CAFFE_ROOT

#update cmake version from default 3.10 to latest
RUN pip3 install --upgrade pip && \
    pip3 install --upgrade cmake && \
    cmake --version

#Hack for libboost-python binding when both python2 and python3 present.    
RUN cd /usr/lib/x86_64-linux-gnu && \
    unlink libboost_python.so && \
    unlink libboost_python.a && \
    ln -s libboost_python-py36.so libboost_python.so && \
    ln -s libboost_python-py36.a libboost_python.a && \
    cd -

#Start Building
RUN git clone https://github.com/foss-for-synopsys-dwc-arc-processors/synopsys-caffe.git . && \
    pip3 install --upgrade pip && \
    cd python && for req in $(cat requirements.txt) pydot; do pip3 install $req; done && cd .. && \
    mkdir build && cd build && \
    cmake -DCPU_ONLY=1 .. && \
    make -j"$(nproc)" && \
    make runtest

ENV PYCAFFE_ROOT $CAFFE_ROOT/python
ENV PYTHONPATH $PYCAFFE_ROOT:$PYTHONPATH
ENV PATH $CAFFE_ROOT/build/tools:$PYCAFFE_ROOT:$PATH
RUN echo "$CAFFE_ROOT/build/lib" >> /etc/ld.so.conf.d/caffe.conf && ldconfig

# Building image END


# install the notebook package
RUN pip3 install notebook jupyterlab

# Copy notebooks

COPY ./ ${HOME}/Notebooks/

RUN chown -R ${NOTEBOOK_UID} ${HOME}
USER ${USER}


ENV PATH="${PATH}:${HOME}/.dotnet/tools"

RUN echo "$PATH"

# hack for scipy bug 
RUN pip3 uninstall -y scipy &&  pip3 install scipy
RUN pip3 uninstall -y pyyaml &&  pip3 install pyyaml

# Set root to Notebooks
WORKDIR ${HOME}/Notebooks/
