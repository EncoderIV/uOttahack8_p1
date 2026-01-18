
ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

NAME=uottahack_2



#This has to be included last
include $(MKFILES_ROOT)/qtargets.mk
