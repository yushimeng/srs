docker run --cap-add=SYS_PTRACE --privileged \
-it --platform linux/amd64  \
--name dev \
-v `pwd`:/opt/srs \
-p 8688:8688 \
--net host \
hub.rongcloud.net/rtc/stream-build:dev bash