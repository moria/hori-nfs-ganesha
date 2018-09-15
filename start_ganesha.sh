echo "started ganesha"
/sbin/rpcbind -w
/usr/bin/ganesha.nfsd
while true
do
    sleep 1
done
