CC := cl
CFLAGS := -Zi /EHsc /std:c++latest /DEBUG:FULL
LIBS := xinput.lib ws2_32.lib winmm.lib user32.lib gdi32.lib msvcrt.lib C:/VulkanSDK/1.1.108.0/Lib/vulkan-1.lib C:/dependencies/Lua/lib/lua5.1.lib

DEF := /DGLM_ENABLE_EXPERIMENTAL /DSTB_IMAGE_IMPLEMENTATION /D_MBCS /DDEVELOPMENT_MODE
INCS := /I C:/dependencies/ /I C:/VulkanSDK/1.1.108.0/Include /I C:/dependencies/stb-master/ /I C:/dependencies/Lua/include/
GCCINC := 

SRCDIR := .
OBJDIR := ../binaries
BINDIR := ../binaries
CPPS := $(wildcard *.cpp)
OBJS := $(CPPS:.cpp=.obj)
DEPFILES := $(CPPS:.cpp=.dep)

all: saska.exe TAGS

TAGS:
	etags *.cpp *.hpp

saska.exe: $(OBJS)
	$(CC) $(CFLAGS) /Fe$@ $(DEF) $(INCS) $(OBJS) $(LIBS)

%.obj: %.cpp
	$(CC) -Zi /EHsc /c /Fo./ $(DEF) $(INCS) $< /std:c++latest

%.dep: %.cpp
	gcc -MM -DGLM_ENABLE_EXPERIMENTAL $(GCCINC) $< | sed 's/.o:/.obj:/' > $@

clean:
	rm *.exe *.obj *.ilk *.pdb *.dep

-include $(DEPFILES)
