BINDIR := ./SPV/
VERTS := $(wildcard *.vert)
VERTS_SPV := $(patsubst %,$(BINDIR)%,$(VERTS:.vert=.vert.spv))

GEOM := $(wildcard *.geom)
GEOM_SPV := $(patsubst %,$(BINDIR)%,$(GEOM:.geom=.geom.spv))

FRAG := $(wildcard *.frag)
FRAG_SPV := $(patsubst %,$(BINDIR)%,$(FRAG:.frag=.frag.spv))

all: $(VERTS_SPV) $(GEOM_SPV) $(FRAG_SPV)

$(BINDIR)%.vert.spv: %.vert
	C:/VulkanSDK/1.1.108.0/Bin32/glslangValidator.exe -V -o $(BINDIR)$<.spv $<

$(BINDIR)%.geom.spv: %.geom
	C:/VulkanSDK/1.1.108.0/Bin32/glslangValidator.exe -V -o $(BINDIR)$<.spv $<

$(BINDIR)%.frag.spv: %.frag
	C:/VulkanSDK/1.1.108.0/Bin32/glslangValidator.exe -V -o $(BINDIR)$<.spv $<
