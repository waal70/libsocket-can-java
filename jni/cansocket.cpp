#include<string>
#include<algorithm>
#include<utility>

#include<cstring>
#include<cstddef>
#include<cerrno>
#include<syslog.h>

#include<sstream>
#include<string.h>

#include <vector>
#include <climits>
#include <memory>

extern "C" {
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
}

//#ifdef (ANDROID) || defined(__ANDROID__)
//#include "jni.h"
//#else
#include "org_waal70_canbus_CanSocket.h"
//#endif

static const int ERRNO_BUFFER_LEN = 1024;

typedef unsigned char BYTE;

static void throwException(JNIEnv *env, const std::string& exception_name,
		const std::string& msg) {
	const jclass exception = env->FindClass(exception_name.c_str());
	if (exception == NULL) {
		return;
	}
	env->ThrowNew(exception, msg.c_str());
}

static void throwIOExceptionMsg(JNIEnv *env, const std::string& msg) {
	throwException(env, "java/io/IOException", msg);
}

static void throwIOExceptionErrno(JNIEnv *env, const int exc_errno) {
	char message[ERRNO_BUFFER_LEN];
	const char * const msg = (char *) strerror_r(exc_errno, message,
			ERRNO_BUFFER_LEN);
	if (((long) msg) == 0) {
		// POSIX strerror_r, success
		throwIOExceptionMsg(env, std::string(message));
	} else if (((long) msg) == -1) {
		// POSIX strerror_r, failure
		// (Strictly, POSIX only guarantees a value other than 0. The safest
		// way to implement this function is to use C++ and overload on the
		// type of strerror_r to accurately distinguish GNU from POSIX. But
		// realistic implementations will always return -1.)
		snprintf(message, ERRNO_BUFFER_LEN, "errno %d", exc_errno);
		throwIOExceptionMsg(env, std::string(message));
	} else {
		// glibc strerror_r returning a string
		throwIOExceptionMsg(env, std::string(msg));
	}
}

static void throwIllegalArgumentException(JNIEnv *env,
		const std::string& message) {
	throwException(env, "java/lang/IllegalArgumentException", message);
}

static void throwOutOfMemoryError(JNIEnv *env, const std::string& message) {
	throwException(env, "java/lang/OutOfMemoryError", message);
}

static jint newCanSocket(JNIEnv *env, int socket_type, int protocol) {
	const int fd = socket(PF_CAN, socket_type, protocol);
	if (fd != -1) {
		return fd;
	}
	throwIOExceptionErrno(env, errno);
	return -1;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1openSocketRAW(
		JNIEnv *env, jclass obj) {
	return newCanSocket(env, SOCK_RAW, CAN_RAW);
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1openSocketBCM(
		JNIEnv *env, jclass obj) {
	return newCanSocket(env, SOCK_DGRAM, CAN_BCM);
}

JNIEXPORT void JNICALL Java_org_waal70_canbus_CanSocket__1close
(JNIEnv *env, jclass obj, jint fd)
{
	if (close(fd) == -1) {
		throwIOExceptionErrno(env, errno);
	}
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1discoverInterfaceIndex(
		JNIEnv *env, jclass clazz, jint socketFd, jstring ifName) {
	struct ifreq ifreq;
	const jsize ifNameSize = env->GetStringUTFLength(ifName);
	if (ifNameSize > IFNAMSIZ - 1) {
		throwIllegalArgumentException(env, "illegal interface name");
		return -1;
	}

	/* fetch interface name */
	memset(&ifreq, 0x0, sizeof(ifreq));
	env->GetStringUTFRegion(ifName, 0, ifNameSize, ifreq.ifr_name);
	if (env->ExceptionCheck() == JNI_TRUE) {
		return -1;
	}
	/* discover interface id */
	const int err = ioctl(socketFd, SIOCGIFINDEX, &ifreq);
	if (err == -1) {
		throwIOExceptionErrno(env, errno);
		return -1;
	} else {
		return ifreq.ifr_ifindex;
	}
}

JNIEXPORT jstring JNICALL Java_org_waal70_canbus_CanSocket__1discoverInterfaceName(
		JNIEnv *env, jclass obj, jint fd, jint ifIdx) {
	struct ifreq ifreq;
	memset(&ifreq, 0x0, sizeof(ifreq));
	ifreq.ifr_ifindex = ifIdx;
	if (ioctl(fd, SIOCGIFNAME, &ifreq) == -1) {
		throwIOExceptionErrno(env, errno);
		return NULL;
	}
	const jstring ifname = env->NewStringUTF(ifreq.ifr_name);
	return ifname;
}

JNIEXPORT void JNICALL Java_org_waal70_canbus_CanSocket__1bindToSocket
(JNIEnv *env, jclass obj, jint fd, jint ifIndex)
{
	struct sockaddr_can addr;
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifIndex;
	if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
		throwIOExceptionErrno(env, errno);
	}
}

JNIEXPORT void JNICALL Java_org_waal70_canbus_CanSocket__1sendFrame
(JNIEnv *env, jclass obj, jint fd, jint if_idx, jint canid, jbyteArray data)
{
	const int flags = 0;
	ssize_t nbytes;
	struct sockaddr_can addr;
	struct can_frame frame;
	memset(&addr, 0, sizeof(addr));
	memset(&frame, 0, sizeof(frame));
	addr.can_family = AF_CAN;
	addr.can_ifindex = if_idx;
	const jsize len = env->GetArrayLength(data);
	if (env->ExceptionCheck() == JNI_TRUE) {
		return;
	}
	frame.can_id = canid;
	frame.can_dlc = static_cast<__u8>(len);
	env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte *>(&frame.data));
	if (env->ExceptionCheck() == JNI_TRUE) {
		return;
	}
	nbytes = sendto(fd, &frame, sizeof(frame), flags,
			reinterpret_cast<struct sockaddr *>(&addr),
			sizeof(addr));
	if (nbytes == -1) {
		throwIOExceptionErrno(env, errno);
	} else if (nbytes != sizeof(frame)) {
		throwIOExceptionMsg(env, "send partial frame");
	}
}

JNIEXPORT jobject JNICALL Java_org_waal70_canbus_CanSocket__1recvFrame(
		JNIEnv *env, jclass obj, jint fd) {
	const int flags = 0;
	ssize_t nbytes;
	struct sockaddr_can addr;
	socklen_t len = sizeof(addr);
	struct can_frame frame;
	memset(&addr, 0, sizeof(addr));
	memset(&frame, 0, sizeof(frame));
	nbytes = recvfrom(fd, &frame, sizeof(frame), flags,
			reinterpret_cast<struct sockaddr *>(&addr), &len);
	if (len != sizeof(addr)) {
		throwIllegalArgumentException(env, "illegal AF_CAN address");
		return NULL;
	}
	if (nbytes == -1) {
		throwIOExceptionErrno(env, errno);
		return NULL;
	} else if (nbytes != sizeof(frame)) {
		throwIOExceptionMsg(env, "invalid length of received frame");
		return NULL;
	}
	const jsize fsize = static_cast<jsize>(std::min(
			static_cast<size_t>(frame.can_dlc),
			static_cast<size_t>(nbytes - offsetof(struct can_frame, data))));
	const jclass can_frame_clazz = env->FindClass("org/waal70/canbus/"
			"CanSocket$CanFrame");
	if (can_frame_clazz == NULL) {
		return NULL;
	}
	const jmethodID can_frame_cstr = env->GetMethodID(can_frame_clazz, "<init>",
			"(II[B)V");
	if (can_frame_cstr == NULL) {
		return NULL;
	}
	const jbyteArray data = env->NewByteArray(fsize);
	if (data == NULL) {
		if (env->ExceptionCheck() != JNI_TRUE) {
			throwOutOfMemoryError(env, "could not allocate ByteArray");
		}
		return NULL;
	}
	env->SetByteArrayRegion(data, 0, fsize,
			reinterpret_cast<jbyte *>(&frame.data));
	if (env->ExceptionCheck() == JNI_TRUE) {
		return NULL;
	}
	const jobject ret = env->NewObject(can_frame_clazz, can_frame_cstr,
			addr.can_ifindex, frame.can_id, data);
	return ret;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetchInterfaceMtu(
		JNIEnv *env, jclass obj, jint fd, jstring ifName) {
	struct ifreq ifreq;

	const jsize ifNameSize = env->GetStringUTFLength(ifName);
	if (ifNameSize > IFNAMSIZ - 1) {
		throwIllegalArgumentException(env, "illegal interface name");
		return -1;
	}
	memset(&ifreq, 0x0, sizeof(ifreq));
	env->GetStringUTFRegion(ifName, 0, ifNameSize, ifreq.ifr_name);
	if (env->ExceptionCheck() == JNI_TRUE) {
		return -1;
	}
	if (ioctl(fd, SIOCGIFMTU, &ifreq) == -1) {
		throwIOExceptionErrno(env, errno);
		return -1;
	} else {
		return ifreq.ifr_mtu;
	}
}

JNIEXPORT void JNICALL Java_org_waal70_canbus_CanSocket__1setsockopt
(JNIEnv *env, jclass obj, jint fd, jint op, jint stat)
{
	const int _stat = stat;
	if (setsockopt(fd, SOL_CAN_RAW, op, &_stat, sizeof(_stat)) == -1) {
		throwIOExceptionErrno(env, errno);
	}
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1getsockopt(
		JNIEnv *env, jclass obj, jint fd, jint op) {
	int _stat = 0;
	socklen_t len = sizeof(_stat);
	if (getsockopt(fd, SOL_CAN_RAW, op, &_stat, &len) == -1) {
		throwIOExceptionErrno(env, errno);
	}
	if (len != sizeof(_stat)) {
		throwIllegalArgumentException(env,
				"setsockopt return size is different");
		return -1;
	}
	return _stat;
}

/*** constants ***/

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1MTU(
		JNIEnv *env, jclass obj) {
	return CAN_MTU;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1FD_1MTU(
		JNIEnv *env, jclass obj) {
	return CANFD_MTU;
}

/*** ioctls ***/
JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1RAW_1FILTER(
		JNIEnv *env, jclass obj) {
	return CAN_RAW_FILTER;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1RAW_1ERR_1FILTER(
		JNIEnv *env, jclass obj) {
	return CAN_RAW_ERR_FILTER;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1RAW_1LOOPBACK(
		JNIEnv *env, jclass obj) {
	return CAN_RAW_LOOPBACK;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1RAW_1RECV_1OWN_1MSGS(
		JNIEnv *env, jclass obj) {
	return CAN_RAW_RECV_OWN_MSGS;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1fetch_1CAN_1RAW_1FD_1FRAMES(
		JNIEnv *env, jclass obj) {
	return CAN_RAW_FD_FRAMES;
}

/*** ADR MANIPULATION FUNCTIONS ***/

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1getCANID_1SFF(
		JNIEnv *env, jclass obj, jint canid) {
	return canid & CAN_SFF_MASK;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1getCANID_1EFF(
		JNIEnv *env, jclass obj, jint canid) {
	return canid & CAN_EFF_MASK;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1getCANID_1ERR(
		JNIEnv *env, jclass obj, jint canid) {
	return canid & CAN_ERR_MASK;
}

JNIEXPORT jboolean JNICALL Java_org_waal70_canbus_CanSocket__1isSetEFFSFF(
		JNIEnv *env, jclass obj, jint canid) {
	return (canid & CAN_EFF_FLAG) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_waal70_canbus_CanSocket__1isSetRTR(
		JNIEnv *env, jclass obj, jint canid) {
	return (canid & CAN_RTR_FLAG) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_waal70_canbus_CanSocket__1isSetERR(
		JNIEnv *env, jclass obj, jint canid) {
	return (canid & CAN_ERR_FLAG) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1setEFFSFF(JNIEnv *env,
		jclass obj, jint canid) {
	return canid | CAN_EFF_FLAG;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1setRTR(JNIEnv *env,
		jclass obj, jint canid) {
	return canid | CAN_RTR_FLAG;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1setERR(JNIEnv *env,
		jclass obj, jint canid) {
	return canid | CAN_ERR_FLAG;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1clearEFFSFF(
		JNIEnv *env, jclass obj, jint canid) {
	return canid & ~CAN_EFF_FLAG;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1clearRTR(JNIEnv *env,
		jclass obj, jint canid) {
	return canid & ~CAN_RTR_FLAG;
}

JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1clearERR(JNIEnv *env,
		jclass obj, jint canid) {
	return canid & ~CAN_ERR_FLAG;
}

size_t getCopySize(size_t sourceSize, size_t destSize)
{
    return (destSize <= sourceSize ? destSize : sourceSize);
}

void logthis(std::string msg)
{
	openlog("libsocket-can-java native library ", LOG_CONS, LOG_USER);
	std::stringstream strs;
	strs << msg;
	std::string temp_str = strs.str();
	char* char_type = (char*) temp_str.c_str();
	syslog(LOG_INFO, char_type);

}

JNIEXPORT jbyteArray JNICALL Java_org_waal70_canbus_CanSocket__1getFilters(
		JNIEnv *env, jclass obj, jint sock) {

	std::string s; //for logging purposes

	socklen_t size = (socklen_t) (sizeof(int) * 4); //Errrm...this is 4 bytes.

	std::unique_ptr<char[]> rfilter(new char [16]);
	std::unique_ptr<char[]> filters_out(new char [16]);

	logthis(s.append("Before getsockopt"));
	s = "";

	logthis(s.append("socklen_t size: ").append(std::to_string(size)));
	s="";

	logthis(s.append("rfilter size (bytes): ").append(std::to_string(sizeof(rfilter))));
		s = "";

	int result = getsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER,  rfilter.get(), &size );
	if (result == -1) {
		return NULL;
	}
	logthis("After getsockopt");
	s="";
	logthis(s.append("struct size (bytes): ").append(std::to_string(sizeof(struct can_filter))));
	s="";

	memcpy(filters_out.get(), rfilter.get(), 16);


	jbyteArray data = env->NewByteArray(16);

		if (data == NULL) {
				if (env->ExceptionCheck() != JNI_TRUE) {
					throwOutOfMemoryError(env, "could not allocate ByteArray");
				}
				return NULL;
			}


		env->SetByteArrayRegion(data, 0, 16, (jbyte *)filters_out.get());

	return data;
}


JNIEXPORT jint JNICALL Java_org_waal70_canbus_CanSocket__1setFilters(
		JNIEnv *env, jclass obj, jint sock, jstring data) {

	// First, let's convert the jstring to a proper C++-string:
	std::string s; //for logging purposes
	const char *inFilterString = env->GetStringUTFChars(data, NULL);
	int numfilter;
	const char *tempString;
	if (NULL == inFilterString)
		return -1;
	logthis("===== BEGIN setsockopt");
	//Counting the commas will give us the number of filter definitions
	// as one filter def has one comma.
	numfilter = 0;
	tempString = inFilterString;
	while (tempString) {
		numfilter++;
		tempString++; /* hop behind the ',' */
		tempString = strchr(tempString, ','); /* fails if no more commas are found */
	}

	//Now, numfilter contains the number of filters :) Should that be 0, then do an erase
	// of the filters:
	if (numfilter == 0)
		return setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

	struct can_filter rfilter[numfilter];
	//TODO: prevent VLA:
	//attempt 1: struct can_filter *rfilter = new can_filter[numfilter];
	//attempt 2 std::vector<can_filter> rfilter;

	//socklen_t size = sizeof(struct can_filter) * numfilter;

	//Expect a filter definition in the following form (HEX!):
	//"12345678:DFFFFFFF"

	tempString = inFilterString;
	logthis(s.append("Full filter string (setsockopt): ").append(tempString));
	s="";

	for (int i=0; i<numfilter; i++)
	{
		if (i != 0)
				tempString = inFilterString +1; //only hop after the comma on filters after the first;
		inFilterString = strchr(tempString, ',');
		canid_t canid;
		canid_t mask;
		sscanf(tempString, "%x:%x", &canid, &mask);
		rfilter[i].can_id=canid | CAN_EFF_FLAG;
		rfilter[i].can_mask=mask | (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK);
		//currentfilter.can_id=canid | CAN_EFF_FLAG;
		//currentfilter.can_mask = mask | (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK);
		//rfilter.push_back(currentfilter);
		logthis(s.append("setsockopt filter canid[").append(std::to_string(i)).append("]:").append(std::to_string(rfilter[i].can_id)));
		s="";
		logthis(s.append("setsockopt filter mask[").append(std::to_string(i)).append("]:").append(std::to_string(rfilter[i].can_mask)));
		s="";

		if (inFilterString == NULL)
			logthis("NULL");
	}

	int result = setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter,
			sizeof(rfilter));
	logthis(s.append("rfilter size after setsockopt: ").append(std::to_string(sizeof(rfilter))));
		s="";

	return result;
}
