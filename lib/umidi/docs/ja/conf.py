# Configuration file for Japanese documentation
# Inherits from parent conf.py

import sys
import os
sys.path.insert(0, os.path.abspath('..'))
from conf import *

# Override language
language = 'ja'
html_title = 'umidi ドキュメント'

# Language switcher context
html_context = {
    'languages': [
        ('en', '/'),
        ('ja', '/ja/'),
    ],
    'current_language': 'ja',
    'display_github': False,
}

# Use parent templates
templates_path = ['../_templates']

# Disable exhale for Japanese (API reference is English only)
extensions = [ext for ext in extensions if ext != 'exhale']
exhale_args = {}
