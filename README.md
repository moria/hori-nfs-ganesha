# hori-nfs-ganesha
ganesha-docker

docker run -d --privileged --cap-add SYS_ADMIN --cap-add DAC_READ_SEARCH --name nfs-ganesha  -v /data/docker-gsh-mnt:/gsh-export:rw ganesha-docker-test01
