# Vygeneruje Source/Translations.cpp zo slovníka tools/en_ui.py
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from en_ui import T
out = ['// VYGENEROVANÉ tools/gen_translations.py – needitovať ručne', '#include "Common.h"', '#include <unordered_map>', '#include <string>', '',
       'namespace kp', '{', 'String tr (const char* utf8)', '{',
       '    if (! english || utf8 == nullptr) return String::fromUTF8 (utf8);',
       '    static const std::unordered_map<std::string, const char*> m = {']
for k, v in T.items():
    out.append('        { "%s", "%s" },' % (k, v))
out += ['    };', '    auto it = m.find (utf8);', '    return String::fromUTF8 (it != m.end() ? it->second : utf8);', '}', '}', '']
open(os.path.join(os.path.dirname(__file__), '..', 'Source', 'Translations.cpp'), 'w').write('\n'.join(out))
