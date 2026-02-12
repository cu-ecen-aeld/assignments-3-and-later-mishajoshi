#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}
OUTDIR=$(realpath ${OUTDIR})

########################################
# Kernel Build
########################################

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi

if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Kernel build steps
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

########################################
# Root Filesystem Setup
########################################

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,var}
mkdir -p ${OUTDIR}/rootfs/usr/{bin,lib,sbin}

########################################
# BusyBox Build
########################################

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
else
    cd busybox
fi

# Make and install busybox
make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install

########################################
# Library Dependencies
########################################

cd ${OUTDIR}/rootfs

SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 lib
cp ${SYSROOT}/lib64/libc.so.6 lib64
cp ${SYSROOT}/lib64/libm.so.6 lib64
cp ${SYSROOT}/lib64/libresolv.so.2 lib64

########################################
# Device Nodes
########################################

sudo mknod -m 666 dev/null c 1 3 || true
sudo mknod -m 600 dev/console c 5 1 || true

########################################
# Build Writer Utility
########################################

echo "Cross-compiling writer for ARM..."
cd ${FINDER_APP_DIR}

# Clean any previous build (optional)
make clean || true

# Direct cross-compile using ARM toolchain
${CROSS_COMPILE}gcc -Wall -Werror -O2 -o ${OUTDIR}/writer writer.c

# Copy to rootfs home
cp ${OUTDIR}/writer ${OUTDIR}/rootfs/home/writer
chmod +x ${OUTDIR}/rootfs/home/writer

########################################
# Copy Finder Scripts and Conf
########################################

cp finder.sh finder-test.sh autorun-qemu.sh ${OUTDIR}/rootfs/home/

mkdir -p ${OUTDIR}/rootfs/home/conf
cp ../conf/username.txt ${OUTDIR}/rootfs/home/conf/
cp ../conf/assignment.txt ${OUTDIR}/rootfs/home/conf/

# Fix path inside finder-test.sh
sed -i 's|\.\./conf/assignment.txt|conf/assignment.txt|' ${OUTDIR}/rootfs/home/finder-test.sh

chmod +x ${OUTDIR}/rootfs/home/*.sh

# Patch shebang: /bin/bash → /bin/sh (BusyBox compatible)
sed -i '1s|^#! */bin/bash|#!/bin/sh|' ${OUTDIR}/rootfs/home/finder.sh

########################################
# Create init Script
########################################

cat > ${OUTDIR}/rootfs/init << 'EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
echo "Boot Successful"
exec /bin/sh
EOF

chmod +x ${OUTDIR}/rootfs/init

########################################
# Set Ownership
########################################

sudo chown -R root:root ${OUTDIR}/rootfs

########################################
# Create initramfs
########################################

cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio

echo "Build Complete"
echo "Image: ${OUTDIR}/Image"
echo "Initramfs: ${OUTDIR}/initramfs.cpio.gz"
