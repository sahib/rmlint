resources:
	@glib-compile-resources shredder/resources/shredder.gresource.xml --sourcedir shredder/resources

schema:
	@mkdir -p ~/.glib-schemas
	@cp shredder/resources/org.gnome.Shredder.gschema.xml ~/.glib-schemas
	@glib-compile-schemas ~/.glib-schemas

run: resources schema
	@GSETTINGS_SCHEMA_DIR=~/.glib-schemas/ python3 -c "import shredder; shredder.run_gui()"

all: run
