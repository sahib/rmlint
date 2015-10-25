resources:
	@glib-compile-resources shredder/resources/shredder.gresource.xml --sourcedir shredder/resources

schema:
	@mkdir ~/.glib-schemas -p
	@cp shredder/resources/org.gnome.Shredder.gschema.xml ~/.glib-schemas
	@glib-compile-schemas ~/.glib-schemas

run: resources schema
	@GSETTINGS_SCHEMA_DIR=~/.glib-schemas/ python3 shredder/__main__.py

all: run
