"""Configuration file for the Sphinx documentation builder."""
from datetime import date
from pathlib import Path

# -- Project information -----------------------------------------------------
project = 'rmlint'
author = 'Christopher Pahl, Daniel Thomas, Vassili Tchersky and Cebtenzzre'
copyright = f'2010-{date.today().year}, {author}'

release = (Path(__file__).parent.parent / '.version').read_text().strip()
version, _, codename = release.partition(' ')

# for _templates/sidebar/brand.html
html_context = {'codename': codename}

# -- General configuration ---------------------------------------------------
needs_sphinx = '8.0'

# use of ':math:'
extensions = ['sphinx.ext.mathjax']

root_doc = 'index'
language = 'en'
exclude_patterns = ['_build']

# -- Options for HTML output -------------------------------------------------
html_theme = 'furo'
html_title = f'rmlint ({release}) documentation'
html_static_path = ['_static']
templates_path = ['_templates']
html_last_updated_fmt = '%b %d, %Y'
html_show_sphinx = False

RMLINT_CSS = {"font-stack--headings": '"Vollkorn", serif'}
html_theme_options = {
    'source_repository': 'https://github.com/sahib/rmlint/',
    'source_branch': 'develop',
    'source_directory': 'docs/',
    'light_logo': 'logo.png',
    'dark_logo': 'logo-dark.png',
    'light_css_variables': RMLINT_CSS,
    'dark_css_variables': RMLINT_CSS,
}

# -- Options for manual page output --------------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    ('rmlint.1', 'rmlint', 'find duplicate files and other space waste efficiently',
     ['Christopher Pahl', 'Daniel Thomas', 'Vassili Tchersky', 'Cebtenzzre'], 1)
]

# If true, show URL addresses after external links.
man_show_urls = False
