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
ADD start_ganesha.sh start_ganesha.sh 
RUN pwd
RUN yum -y install rpcbind
#RUN systemctl start rpcbind.service
ADD ntirpc ntirpc
RUN cmake --version && ls -l /usr/lib64
RUN cd ntirpc && mkdir build && cd build && cmake ../ && make && make install
RUN echo "/usr/local/lib64" > /etc/ld.so.conf.d/ntirpc.conf && ldconfig
#RUN cd nfs-ganesha/ && mkdir build && cd build && pwd && cmake ../src -DUSE_SYSTEM_NTIRPC=ON -DUSE_NFS3=off -DUSE_NLM=off && make && make install
RUN cd nfs-ganesha/ && mkdir build && cd build && pwd && cmake ../src -DUSE_SYSTEM_NTIRPC=ON && make && make install
ADD etc/ganesha /etc/ganesha
RUN mkdir /gsh-export
RUN ls -l /usr/bin/ganesha.nfsd
EXPOSE 111 111/udp 662 2049 38465-38467

ADD VERSION VERSION

CMD ["/bin/sh", "./start_ganesha.sh"]
