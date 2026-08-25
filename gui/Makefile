SCHEMA_SRC    := shredder/resources/org.gnome.Shredder.gschema.xml
SCHEMA_DIR    := .glib-schemas
SCHEMA_OUT    := $(SCHEMA_DIR)/gschemas.compiled

RESOURCE_XML  := shredder/resources/shredder.gresource.xml
RESOURCE_OUT  := shredder/resources/shredder.gresource
RESOURCE_DEPS := $(shell glib-compile-resources --generate-dependencies \
                   --sourcedir shredder/resources $(RESOURCE_XML) 2>/dev/null)

.PHONY: all resources schema run clean

all: run

resources: $(RESOURCE_OUT)

$(RESOURCE_OUT): $(RESOURCE_XML) $(RESOURCE_DEPS)
	@glib-compile-resources $(RESOURCE_XML) --sourcedir shredder/resources

schema: $(SCHEMA_OUT)

$(SCHEMA_OUT): $(SCHEMA_SRC)
	@mkdir -p $(SCHEMA_DIR)
	@glib-compile-schemas --targetdir=$(SCHEMA_DIR) shredder/resources

run: $(RESOURCE_OUT) $(SCHEMA_OUT)
	@PATH="$$(realpath ../):$$PATH" \
		GSETTINGS_BACKEND=memory \
		GSETTINGS_SCHEMA_DIR=$(abspath $(SCHEMA_DIR)) \
		GIO_USE_VFS=local \
		python3 -Wd -c "import shredder; shredder.run_gui()"

clean:
	@rm -f $(RESOURCE_OUT) $(SCHEMA_OUT)
	@rmdir $(SCHEMA_DIR)
	@find shredder -name '*.py[co]' -delete
	@find shredder -type d -name '__pycache__' -empty -delete
