CGgminer v4.6.1 - HexMiner-Scrypt
==============

CGMiner v4.6.1 with Hexminer ASIC-SCRYPT support.

This file describes cgminer specific settings and options.

For general CGMiner information refer to doc/README .

## Hexminer : ##

This code is forked from original cgminer v4.6.1 and official patches for hexminer were added.

Extranonce patch from nicehash were included too.

![](https://github.com/wareck/cgminer-hexminer/blob/master/patches_and_diy/images/hexminer.jpg)


How to build scrypt version:

	sudo apt-get update
	sudo apt-get install build-essential autoconf automake libtool pkg-config libcurl4-openssl-dev libudev-dev \
	libjansson-dev libncurses5-dev libudev-dev libjansson-dev
	cd hexminer_scrypt
	./autogen.sh
	./configure --enable-scrypt --enable-hexminers
	make
	sudo make install


### Option Summary : 

For each kind of hexminer you can have some options...

For list them: 

	./cgminer --help

```
--hexminers-options <arg> Set hexminers options asic_count:freq
--hexminers-voltage <arg> Set hexminers core voltage, in millivolts
--set_default_to_s  Handle USB detect errors as hexs
--hexminers-chip-mask <arg> Set hexminers eneable or disable chips
--hexminers-set-diff-to-one <arg> Set hexminers ASIC difficulty to one
```

Use this following example for base :
	
	./cgminer -o stratum+tcp://us-east.batltc.com:3333 -u myname.worker -p x --hexminers-set-diff-to-one 0 \
	--hexminers-chip-mask 255 --hexminers-voltage 1000 --hexminer8-options 8:240

![](https://github.com/wareck/cgminer-hexminer/blob/master/patches_and_diy/images/hexminer2.jpg)

