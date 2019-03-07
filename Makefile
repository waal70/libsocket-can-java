SHELL=/bin/bash
CXX=g++
NAME:=libsocket-can-java

## Recognize OS in order to allow some make-targets to also run
##	on MacOS:
UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		JAVA_HOME=/usr
		SKIPJAVAHOME=true
	endif

### JAVA_HOME
ifndef SKIPJAVAHOME
        JAVA_HOME=$(shell readlink -f /usr/bin/javac | sed "s:bin/javac::")
endif
##JAVA_HOME=/usr

JAVA_INCLUDES=-I$(JAVA_HOME)/include/linux -I$(JAVA_HOME)/include
JAVA=$(JAVA_HOME)/bin/java
JAVAC=$(JAVA_HOME)/bin/javac
JAVAH=$(JAVA_HOME)/bin/javah
JAR=$(JAVA_HOME)/bin/jar
JAVA_SRC:=$(shell find src -type f -and -name '*.java')
JAVA_TEST_SRC:=$(shell find src.test -type f -and -name '*.java')
JNI_SRC:=$(shell find jni -type f -and -regex '^.*\.\(cpp\|h\)$$')
JAVA_DEST=classes
JAVA_TEST_DEST=classes.test
LIB_DEST=lib
JAR_DEST=dist
JAR_DEST_FILE=$(JAR_DEST)/$(NAME).jar
JAR_MANIFEST_FILE=META-INF/MANIFEST.MF
DIRS=stamps obj $(JAVA_DEST) $(JAVA_TEST_DEST) $(LIB_DEST) $(JAR_DEST)
JNI_DIR=jni
JNI_CLASSES=org.waal70.canbus.CanSocket
JAVAC_FLAGS=-g -Xlint:all
CXXFLAGS=-I./include -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions \
-fstack-protector --param=ssp-buffer-size=4 -fPIC -Wno-unused-parameter \
-pedantic -D_REENTRANT -D_GNU_SOURCE \
$(JAVA_INCLUDES)
SONAME=jni_socketcan
LDFLAGS=-Wl,-soname,$(SONAME)
REMOTE=169.254.23.187
REMOTEDIR=/home/awaal/libsocket-can-java
LOCALINCLUDE=../VolvoCANBUS/src/main/resources/lib

.DEFAULT_GOAL := all
.LIBPATTERNS :=
.SUFFIXES:

.PHONY: all
all: stamps/create-jar stamps/compile-test

.PHONY: clean
clean:
	$(RM) -r $(DIRS) $(STAMPS) $(filter %.h,$(JNI_SRC))
	$(RM) -r $(JNI_DIR)/*.h
	$(RM) -r $(JNI_DIR)/*.gch

stamps/dirs:
	mkdir $(DIRS)
	@touch $@

stamps/compile-src: stamps/dirs $(JAVA_SRC)
	$(JAVAC) $(JAVAC_FLAGS) -d $(JAVA_DEST) $(sort $(JAVA_SRC))
	@touch $@

stamps/compile-test: stamps/compile-src $(JAVA_TEST_SRC)
	$(JAVAC) $(JAVAC_FLAGS) -cp $(JAVA_DEST) -d $(JAVA_TEST_DEST) \
		$(sort $(JAVA_TEST_SRC))
	@touch $@

stamps/generate-jni-h: stamps/compile-src
	$(JAVAH) -jni -d $(JNI_DIR) -classpath $(JAVA_DEST) \
		$(JNI_CLASSES)
	@touch $@

stamps/compile-jni: stamps/generate-jni-h $(JNI_SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $(LIB_DEST)/lib$(SONAME).so \
		$(sort $(filter %.cpp,$(JNI_SRC)))
	@touch $@

stamps/create-jar: stamps/compile-jni $(JAR_MANIFEST_FILE)
	$(JAR) cMf $(JAR_DEST_FILE) $(JAR_MANIFEST_FILE) lib -C $(JAVA_DEST) .
	@touch $@
	
stamps/pi-xfer: clean
	##Pre-req: make sure the local key-pair is also
	##allowed on the pi:
	## on local machine (in ~/.ssh): ssh-keygen -t rsa
	## cat id_rsa.pub | ssh <user>@$(REMOTE) ' cat >>.ssh/authorized_keys'
	## ssh-keyscan $(REMOTE) >> ~/.ssh/known_hosts
	ssh $(REMOTE) 'mkdir -p '$(REMOTEDIR)''
	scp -r ./* $(REMOTE):$(REMOTEDIR) 
	
stamps/pi-build: stamps/pi-xfer
	ssh $(REMOTE) 'make clean --directory=$(REMOTEDIR)'
	ssh $(REMOTE) 'make --directory=$(REMOTEDIR)'
	scp $(REMOTE):$(REMOTEDIR)/$(LIB_DEST)/lib$(SONAME).so $(LOCALINCLUDE)

.PHONY: check
check: stamps/create-jar stamps/compile-test
	$(JAVA) -ea -cp $(JAR_DEST_FILE):$(JAVA_TEST_DEST) \
		-Xcheck:jni \
		org.waal70.canbus.CanSocketTest
