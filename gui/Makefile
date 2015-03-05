resources:
	glib-compile-resources app/resources/app.gresource.xml --sourcedir app/resources

schema:
	mkdir ~/.glib-schemas -p
	cp app/resources/org.gnome.Rmlint.gschema.xml ~/.glib-schemas
	glib-compile-schemas ~/.glib-schemas

run: resources schema
	GSETTINGS_SCHEMA_DIR=~/.glib-schemas/ python3 app/__main__.py

all: run
