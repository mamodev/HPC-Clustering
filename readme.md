StreamKM++ Paper:
https://epubs.siam.org/doi/pdf/10.1137/1.9781611972900.16
https://dl.acm.org/doi/10.1145/2133803.2184450

CluStream Paper:
https://www.vldb.org/conf/2003/papers/S04P02.pdf

Impl:
https://github.com/NicerWang/CluStream?tab=readme-ov-file


https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=9723527


Original paper on coresets proof:
https://arxiv.org/pdf/1810.12826



Disable CPU 0;
sudo vim /boot/loader/entries/2025-01-11_22-27-08_linux.conf 
and add isolcpus=0,8
reboot

<!-- powersave -->
<!-- sudo cpupower set --epp balance_performance -->

<!-- info about cpupower -->
sudo cpupower frequency-info

current boost status: cat /sys/devices/system/cpu/cpu0/cpufreq/boost

sudo sh -c "echo 0 > /sys/devices/system/cpu/cpu0/cpufreq/boost"
sudo cpupower -c 0 set --epp performance
sudo cpupower -c 0 frequency-set -g performance
sudo cpupower -c 0 frequency-set -f 3900MHz


sudo cpupower set --epp performance
sudo cpupower frequency-set -g performance
