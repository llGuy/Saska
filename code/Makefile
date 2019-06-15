CC := cl
CFLAGS := /std:c++latest -Zi /EHsc
LIBS := user32.lib User32.lib Gdi32.lib Shell32.lib kernel32.lib gdi32.lib msvcrt.lib msvcmrt.lib C:/VulkanSDK/1.1.70.1/Lib/vulkan-1.lib C:/dependencies/glfw-3.2.1.bin.WIN64/lib-vc2015/glfw3.lib
DEF := /DGLM_ENABLE_EXPERIMENTAL /DSTB_IMAGE_IMPLEMENTATION /DGLEW_STATIC
INCS := /I C:/dependencies /I C:/dependencies/xml_parser /I C:/dependencies/c++ /I C:/VulkanSDK/1.1.70.1/Include /I C:/dependencies/glfw-3.2.1.bin.WIN64/include /I C:/dependencies/stb-master /I C:/dependencies/json-loader/include/
GCCDEF := -DGLM_ENABLE_EXPERIMENTAL -DSTB_IMAGE_IMPLEMENTATION -DGLEW_STATIC
GCCINC := -IC:/dependencies/GLEW/include -IC:/dependencies/GLFW/include -IC:/dependencies/json-loader/include/nlohmann -IC:/dependencies -IC:/dependencies/xml_parser -IC:/dependencies/c++

SRCDIR := ./
OBJDIR := ./
BINDIR := ./
CPPS := $(wildcard *.cpp)
OBJS := $(patsubst %,$(OBJDIR)%,$(CPPS:.cpp=.obj))
DEPFILES := $(patsubst %,$(OBJDIR)%,$(CPPS:.cpp=.dep))
EXENAME := saska.exe

$(BINDIR)$(EXENAME): $(OBJS) TAGS
	$(CC) $(CFLAGS) /DEBUG:FULL /Fe$@ $(DEF) $(INCS) $(OBJS) $(LIBS)

tags: $(OBJS)
	etags *.cpp *.hpp

$(OBJDIR)%.obj: %.cpp
	$(CC) -Zi /EHsc /c /Fo$(OBJDIR) $(DEF) $(INCS) $< /std:c++latest

$(OBJDIR)%.dep: %.cpp
	gcc -MM $(GCCDEF) $(GCCINC) $< | sed 's/.o:/.obj:/' > $@

clean:
	rm $(BINDIR)*.exe $(OBJDIR)*.obj $(OBJDIR)*.ilk $(OBJDIR)*.pdb *.dep

run: $(BINDIR)$(EXENAME)
	$(EXENAME)

display:
	cat .debug

debug:
	devenv $(EXENAME)

-include $(DEPFILES)
