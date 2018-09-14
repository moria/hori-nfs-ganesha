FROM centos

MAINTAINER hm

WORKDIR /opt

# project setting

#
RUN yum -y install gcc automake autoconf libtool make
RUN yum -y install git cmake libtool bison flex
RUN yum -y install openssl-devel nfs-utils doxygen redhat-lsb gcc-c++
#RUN yun -y install libgssglue-devel nfs-utils-lib-devel 
ADD nfs-ganesha nfs-ganesha
RUN pwd
ADD ntirpc ntirpc
RUN cmake --version && ls -l /usr/lib64
RUN cd ntirpc && mkdir build && cd build && cmake ../ && make && make install
RUN cd nfs-ganesha/ && mkdir build && cd build && pwd && cmake ../src -DUSE_SYSTEM_NTIRPC=ON -DCMAKE_INSTALL_PREFIX=/usr/local/ganesha && make && make install
RUN ls -l /usr/local/ganesha/bin/ganesha.nfsd
CMD [/usr/local/ganesha/bin/ganesha.nfsd]
