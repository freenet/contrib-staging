#/bin/sh

case `uname -sr` in
MINGW*)
	echo "Building windows .dll's";;
Linux*)
	echo "Building linux .so's";;
FreeBSD*)
	echo "Building linux .so's";;
*)
	echo "Unsupported build environment"
	exit;;
esac

echo "Extracting GMP..."
tar -xzf gmp-4.1.3.tar.gz
echo "Building..."
mkdir bin
mkdir lib
mkdir lib/net
mkdir lib/net/i2p
mkdir lib/net/i2p/util
for x in none pentium pentiummmx pentium2 pentium3 pentium4 k6 k62 k63 athlon
do
	mkdir bin/$x
	cd bin/$x
	../../gmp-4.1.3/configure --build=$x
	make
	../../build_jbigi.sh static
	case `uname -sr` in
	MINGW*)
		cp jbigi.dll ../../lib/net/i2p/util/jbigi-windows-$x.dll;;
	Linux*)
		cp libjbigi.so ../../lib/net/i2p/util/libjbigi-linux-$x.so;;
	FreeBSD*)
		cp libjbigi.so ../../lib/net/i2p/util/libjbigi-freebsd-$x.so;;
	esac
	cd ..
	cd ..
done

#if `uname -sr`  startswith MINGW
#then
# exit
#fi

mkdir bin/dynamic
cd bin/dynamic
../../gmp-4.1.3/configure
make
../../build_jbigi.sh dynamic
case `uname -sr` in
MINGW*)
	cp jbigi.dll ../../lib/jbigi-windows-dynamic.dll;;
Linux*)
	cp libjbigi.so ../../lib/libjbigi-linux-dynamic.so;;
FreeBSD*)
	cp libjbigi.so ../../lib/libjbigi-freebsd-dynamic.so;;
esac
cd ..
cd ..
