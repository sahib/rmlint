# This Makefile is for manual testing only.
# GNU make syntax (shell vs !=), use gmake on BSD.
SCHEMA_DIR    := schemas
SCHEMA_SRC    := $(SCHEMA_DIR)/io.github.sahib.rmlint.Shredder.gschema.xml
SCHEMA_OUT    := $(SCHEMA_DIR)/gschemas.compiled

LOCALE_DIR    := .locale

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

$(SCHEMA_OUT): translations $(SCHEMA_SRC)
	@glib-compile-schemas --strict $(SCHEMA_DIR)

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
		TEXTDOMAINDIR=$(abspath $(LOCALE_DIR)) \
		python3 -Wd -c "import shredder; shredder.run_gui()" $(ARGS)

clean:
	@rm -f $(RESOURCE_OUT) $(SCHEMA_OUT)
	@find shredder -name '*.py[co]' -delete
	@find shredder -type d -name '__pycache__' -empty -delete
	@if [ -d "$(LOCALE_DIR)" ]; then \
		find "$(LOCALE_DIR)" -depth \( -type d -o -name '*.mo' \) -delete; \
	fi
