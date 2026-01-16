# Configuration file for Sphinx documentation builder
# umidi library documentation

import os
import subprocess

# -- Project information -----------------------------------------------------

project = 'umidi'
copyright = '2024, UMI-OS Project'
author = 'UMI-OS Project'
release = '1.0.0'

# -- General configuration ---------------------------------------------------

extensions = [
    'breathe',
    'exhale',
    'myst_parser',
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
]

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

# -- Options for HTML output -------------------------------------------------

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']
html_title = 'umidi Documentation'

# Language switcher in header
html_context = {
    'languages': [
        ('en', '/'),
        ('ja', '/ja/'),
    ],
    'current_language': 'en',
    'display_github': False,
}

html_theme_options = {
    'navigation_depth': 3,
    'collapse_navigation': False,
}

# -- Internationalization ----------------------------------------------------

language = 'en'

# -- Breathe configuration ---------------------------------------------------

breathe_projects = {
    'umidi': '_build/doxygen/xml'
}
breathe_default_project = 'umidi'
breathe_default_members = ('members', 'undoc-members')

# -- Exhale configuration ---------------------------------------------------

exhale_args = {
    # Required arguments
    'containmentFolder':     './api',
    'rootFileName':          'library_root.rst',
    'doxygenStripFromPath':  '..',

    # Optional arguments
    'rootFileTitle':         'API Reference',
    'createTreeView':        True,
    'exhaleExecutesDoxygen': True,
    'exhaleDoxygenStdin':    '''
INPUT                  = ../include/umidi
FILE_PATTERNS          = *.hh *.h
RECURSIVE              = YES
EXCLUDE_PATTERNS       = */test/*
EXTRACT_ALL            = NO
EXTRACT_PRIVATE        = NO
HIDE_UNDOC_MEMBERS     = YES
HIDE_UNDOC_CLASSES     = YES
JAVADOC_AUTOBRIEF      = YES
GENERATE_XML           = YES
XML_PROGRAMLISTING     = YES
PREDEFINED             = __cplusplus=202302L
''',

    # Tree view customization
    'afterTitleDescription': '''
This is the auto-generated API reference for umidi.
For usage examples, see the :doc:`/examples` page.
''',
}

# -- MyST parser configuration ----------------------------------------------

myst_enable_extensions = [
    'colon_fence',
    'deflist',
]

# -- Custom setup -----------------------------------------------------------

def setup(app):
    # Ensure static directory exists
    os.makedirs('_static', exist_ok=True)
