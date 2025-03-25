#!/usr/bin/env python3
# created by sphinx-quickstart on Mon Oct 21 16:42:48 2013.
#
# This file is execfile()d with the current directory set to its containing dir.
#
# Note that not all possible configuration values are present in this
# autogenerated file.
#
# All configuration values have a default; values that are commented out
# serve to show the default.

import sys
import os

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
sys.path.insert(0, os.path.abspath('./'))

#html_theme = 'pydata_sphinx_theme'
html_theme = 'alabaster'

# -- General configuration -----------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
needs_sphinx = '1.3'

# Add any Sphinx extension module names here, as strings. They can be extensions
# coming with Sphinx (named 'sphinx.ext.*') or your custom ones.
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.doctest',
    'sphinx.ext.intersphinx',
    'sphinx.ext.todo',
    'sphinx.ext.coverage',
    'sphinx.ext.mathjax',
    'sphinx.ext.ifconfig',
    'sphinx.ext.graphviz',
    'sphinx.ext.viewcode'
]

# The suffix of source filenames.
source_suffix = '.rst'

# The encoding of source files.
#source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = 'index'

todo_include_todos = True

# General information about the project.
project = 'rmlint documentation'
copyright = '2014-2025, Christopher Pahl, Daniel Thomas, and Cebtenzzre'

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# The short X.Y version.
# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
#today = ''
# Else, today_fmt is used as the format for a strftime call.
#today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
exclude_patterns = ['_build', '_themes/armstrong/README.rst']

# The reST default role (used for this markup: `text`) to use for all documents.
#default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
#add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
#add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
show_authors = True

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = "default"

# A list of ignored prefixes for module index sorting.
#modindex_common_prefix = []


# -- Options for HTML output ---------------------------------------------------

# Theme options are theme-specific and customize the look and feel of a theme
# further.  For a list of options available for each theme, see the
# documentation.

# (Optional) Logo. Should be small enough to fit the navbar (ideally 24x24).
# Path should be relative to the ``_static`` files directory.
html_logo = "logo.png"

# Theme options are theme-specific and customize the look and feel of a
# theme further.
html_theme_options = {
    'logo': 'logo.png',
    'github_user': 'sahib',
    'github_repo': 'rmlint',
    'github_button': True,
    'fixed_sidebar': True,
}

# Add any paths that contain custom themes here, relative to this directory.

# The name for this set of Sphinx documents.  If None, it defaults to
# "<project> v<release> documentation".

try:
    with open('../.version', 'r') as handle:
        version_string = handle.read().strip()
except OSError:
    version_string = '2.x.x'

html_title = 'rmlint ({v}) documentation'.format(v=version_string)

# A shorter title for the navigation bar.  Default is the same as html_title.
#html_short_title = None
github_fork = 'sahib'

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
# html_favicon = '_static/logo.ico'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

# If not '', a 'Last updated on:' timestamp is inserted at every page bottom,
# using the given strftime format.
html_last_updated_fmt = '%b %d, %Y'

# If true, SmartyPants will be used to convert quotes and dashes to
# typographically correct entities.
# html_use_smartypants = False
# smart_quotes = True

html_sidebars = {
    '**': [
        'about.html',
        'searchfield.html',
        'navigation.html',
        'relations.html',
    ]
}

# Additional templates that should be rendered to pages, maps page names to
# template names.
#html_additional_pages = {}

# If false, no module index is generated.
html_domain_indices = True

# If false, no index is generated.
html_use_index = True

# If true, the index is split into individual pages for each letter.
#html_split_index = False

# If true, links to the reST sources are added to the pages.
html_show_sourcelink = True

# If true, "Created using Sphinx" is shown in the HTML footer. Default is True.
html_show_sphinx = False

# If true, "(C) Copyright ..." is shown in the HTML footer. Default is True.
html_show_copyright = True

# If true, an OpenSearch description file will be output, and all pages will
# contain a <link> tag referring to it.  The value of this option must be the
# base URL from which the finished HTML is served.
#html_use_opensearch = ''

# This is the file name suffix for HTML files (e.g. ".xhtml").
#html_file_suffix = None

# Output file base name for HTML help builder.
htmlhelp_basename = 'rmlint'


# -- Options for LaTeX output --------------------------------------------------

latex_elements = {
# The paper size ('letterpaper' or 'a4paper').
#'papersize': 'letterpaper',

# The font size ('10pt', '11pt' or '12pt').
#'pointsize': '10pt',

# Additional stuff for the LaTeX preamble.
#'preamble': '',
}

# -- Options for manual page output --------------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    ('rmlint.1', 'rmlint', 'find duplicate files and other space waste efficiently',
     ['Christopher Pahl', 'Daniel Thomas', 'Cebtenzzre'], 1)
]

# If true, show URL addresses after external links.
man_show_urls = False

# Example configuration for intersphinx: refer to the Python standard library.
intersphinx_mapping = {
    'python': ('http://python.readthedocs.org/en/latest/', None),
    'sphinx': ('http://sphinx.readthedocs.org/en/latest/', None)
}

# Make __init__ and Class.__doc__ visible:
autoclass_content = 'both'
