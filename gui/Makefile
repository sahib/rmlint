# This Makefile is for manual testing only.
# GNU make syntax (shell vs !=), use gmake on BSD.
SCHEMA_SRC    := shredder/resources/org.gnome.Shredder.gschema.xml
SCHEMA_DIR    := .glib-schemas
LOCALE_DIR    := .locale
SCHEMA_OUT    := $(SCHEMA_DIR)/gschemas.compiled

RESOURCE_XML  := shredder/resources/shredder.gresource.xml
RESOURCE_OUT  := shredder/resources/shredder.gresource
RESOURCE_DEPS := $(shell glib-compile-resources --generate-dependencies \
                   --sourcedir shredder/resources $(RESOURCE_XML) 2>/dev/null)

.PHONY: all resources schema run clean translations

all: run

resources: $(RESOURCE_OUT)

$(RESOURCE_OUT): $(RESOURCE_XML) $(RESOURCE_DEPS)
	@glib-compile-resources $(RESOURCE_XML) --sourcedir shredder/resources

schema: $(SCHEMA_OUT)

$(SCHEMA_OUT): $(SCHEMA_SRC)
	@mkdir -p $(SCHEMA_DIR)
	@glib-compile-schemas --targetdir=$(SCHEMA_DIR) shredder/resources

translations:
	@if [ -n "$$LANGUAGE" ]; then \
		$(MAKE) $(LOCALE_DIR)/$$LANGUAGE/LC_MESSAGES/rmlint.mo; \
	fi

$(LOCALE_DIR)/%/LC_MESSAGES/rmlint.mo: ../po/%.po
	@mkdir -p $(@D)
	@msgfmt $< -o $@

run: translations $(RESOURCE_OUT) $(SCHEMA_OUT)
	@PATH="$$(realpath ../):$$PATH" \
		GSETTINGS_BACKEND=memory \
		GSETTINGS_SCHEMA_DIR=$(abspath $(SCHEMA_DIR)) \
		GIO_USE_VFS=local \
		SHREDDER_LOCALEDIR=$(abspath $(LOCALE_DIR)) \
		python3 -Wd -c "import shredder; shredder.run_gui()" $(ARGS)

clean:
	@rm -f $(RESOURCE_OUT) $(SCHEMA_OUT)
	@rmdir $(SCHEMA_DIR)
	@find shredder -name '*.py[co]' -delete
	@find shredder -type d -name '__pycache__' -empty -delete
	@find "$(LOCALE_DIR)" -depth \( -type d -o -name '*.mo' \) -delete
