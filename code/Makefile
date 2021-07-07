ifeq ($(OS),Windows_NT)
	ARCH := win64
else
	ifeq ($(shell uname -s),Darwin)
		ARCH := macos
	else
		ARCH := linux
	endif
endif

# Dependencies:
#   https://github.com/nlohmann/json.git
#   vulkan sdk
#   https://www.glfw.org
#   stb https://github.com/nothings/stb.git
#

ifeq ($(ARCH),win64)
	DEPPATH := C:/dependencies
	CC := cl
	CFLAGS := /std:c++latest -Zi /EHsc
	LIBS := user32.lib User32.lib Gdi32.lib Shell32.lib kernel32.lib gdi32.lib msvcrt.lib msvcmrt.lib C:/VulkanSDK/1.1.70.1/Lib/vulkan-1.lib $(DEPPATH)/glfw-3.2.1.bin.WIN64/lib-vc2015/glfw3.lib
	DEF := /DGLM_ENABLE_EXPERIMENTAL /DSTB_IMAGE_IMPLEMENTATION /DGLEW_STATIC
	INCS := /I $(DEPPATH) /I $(DEPPATH)/xml_parser /I $(DEPPATH)/c++ /I C:/VulkanSDK/1.1.70.1/Include /I $(DEPPATH)/glfw-3.2.1.bin.WIN64/include /I $(DEPPATH)/stb-master /I $(DEPPATH)/json-loader/include/
	GCCDEF := -DGLM_ENABLE_EXPERIMENTAL -DSTB_IMAGE_IMPLEMENTATION -DGLEW_STATIC
	GCCINC := -I$(DEPPATH)/GLEW/include -I$(DEPPATH)/GLFW/include -I$(DEPPATH)/json-loader/include -I$(DEPPATH) -I$(DEPPATH)/xml_parser -I$(DEPPATH)/c++
	EXENAME := .exe
endif

ifeq ($(ARCH),macos)
	DEPPATH := $(HOME)/Development/external
	CC := cc
	CFLAGS := -std=c++17 -stdlib=libc++ -g -Dcpp_no_optional=1 -DGLM_ENABLE_EXPERIMENTAL
	LIBS := -lglfw -lvulkan -framework OpenGL -lc++ -llua
	GCCDEF := -DGLM_ENABLE_EXPERIMENTAL -DSTB_IMAGE_IMPLEMENTATION -DGLEW_STATIC
	GCCINC := -I$(DEPPATH)/glew/include -I$(DEPPATH)/glfw/include  -I$(DEPPATH)/vulkan-sdk/macOS/include -I$(DEPPATH)/stb -I$(DEPPATH)/json/include/ -I/usr/local/include/lua -I$(DEPPATH)/vml/vml
	DEF := $(GCCDEF)
	INCS := $(GCCINC)
	EXENAME := game2
	UNITYEXE := game
	LDFLAGS := -L$(DEPPATH)/vulkan-sdk/macOS/lib -L/usr/local/lib
endif	

SRCDIR:=.
OBJDIR:=obj
BINDIR:=.
CPPS :=$(wildcard *.cpp)
ifeq ($(ARCH),win64)
OBJS := $(patsubst %,$(OBJDIR)/%,$(CPPS:.cpp=.obj))
else
OBJS := $(patsubst %,$(OBJDIR)/%,$(CPPS:.cpp=.o))
endif
DEPFILES := $(patsubst %,$(OBJDIR)%,$(CPPS:.cpp=.dep))


all: directories
	@echo $(patsubst %,$(OBJDIR)/%,$(CPPS))
	@echo $(OBJS)

directories:
	@mkdir -p $(OBJDIR)

tags: $(CPPS)
	etags *.cpp *.hpp

$(BINDIR)/$(UNITYEXE): directories core.cpp
ifeq ($(ARCH),win64)
	$(CC) $(CFLAGS) /DEBUG:FULL /Fe$@ $(DEF) $(INCS) $(OBJS) $(LIBS)
else
	$(CC) $(CFLAGS) -DUNITY_BUILD $(LDFLAGS) -o $@ $(INCS) core.cpp $(LIBS)
endif

$(BINDIR)/$(EXENAME): directories $(OBJS) TAGS
ifeq ($(ARCH),win64)
	$(CC) $(CFLAGS) /DEBUG:FULL /Fe$@ $(DEF) $(INCS) $(OBJS) $(LIBS)
else
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(INCS) $(OBJS) $(LIBS)
endif

ifeq ($(ARCH),win64)
$(OBJDIR)/%.obj: %.cpp
	$(CC) -Zi /EHsc /c /Fo$(OBJDIR) $(DEF) $(INCS) $< /std:c++latest
else
$(OBJDIR)/%.o: %.cpp
	$(CC) $(CFLAGS) $(DEF) $(INCS) -c -o $@ $< 
endif

$(OBJDIR)/%.dep: %.cpp
ifeq ($(ARCH),win64)
	gcc -MM $(GCCDEF) $(GCCINC) $< | sed 's/.o:/.obj:/' > $@
else
	$(CC) -MM $(GCCDEF) $(GCCINC) $< > $@
endif

clean:
	rm $(BINDIR)/*.exe $(OBJDIR)/*.obj $(OBJDIR)/*.ilk $(OBJDIR)/*.pdb $(OBJDIR)/*.dep

run: 
	$(EXENAME)

display:
	cat .debug

debug:
	devenv $(EXENAME)

-include $(DEPFILES)
