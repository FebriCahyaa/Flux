#!/usr/bin/env python3
"""Flux-chan, the Flux Tweaks mascot, drawn as SVG (original artwork).

Writes art/flux_happy.svg and art/flux_sleeping.svg (512x512, transparent).
Render them with art/render.js; the WebUI and the banner use the renders.

    python3 art/mascot.py
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent

HAIR = "#d9d2ff"
HAIR_SHADE = "#a99cf2"
HAIR_DEEP = "#7c6be0"
STREAK = "#5fd6e6"
SKIN = "#ffe9df"
SKIN_SHADE = "#f7c9bb"
HOODIE = "#6a4cf5"
HOODIE_SHADE = "#4f33d1"
HOODIE_LIGHT = "#8c73ff"
LINE = "#2a1f5c"
PHONES = "#2b2842"
GLOW = "#63f0ff"
BLUSH = "#ff8fb1"

DEFS = f"""
  <defs>
    <linearGradient id="iris" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#3b2aa8"/>
      <stop offset="0.55" stop-color="#6f5cff"/>
      <stop offset="1" stop-color="{GLOW}"/>
    </linearGradient>
    <linearGradient id="hair" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#eeeaff"/>
      <stop offset="1" stop-color="{HAIR}"/>
    </linearGradient>
    <linearGradient id="hoodie" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{HOODIE_LIGHT}"/>
      <stop offset="1" stop-color="{HOODIE}"/>
    </linearGradient>
    <radialGradient id="blush">
      <stop offset="0" stop-color="{BLUSH}" stop-opacity="0.75"/>
      <stop offset="1" stop-color="{BLUSH}" stop-opacity="0"/>
    </radialGradient>
    <filter id="soft" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="6" stdDeviation="8" flood-color="#1b1240" flood-opacity="0.25"/>
    </filter>
  </defs>"""

BACK_HAIR = f"""
  <!-- back hair -->
  <path d="M112,236 C100,130 170,62 256,62 C342,62 412,130 400,236 C404,300 396,352 380,384
           C366,360 356,330 350,300 L162,300 C156,330 146,360 132,384 C116,352 108,300 112,236 Z"
        fill="{HAIR_SHADE}"/>
  <path d="M132,384 C126,340 132,310 150,292 L168,318 C160,340 150,364 132,384 Z" fill="{HAIR_DEEP}" opacity="0.55"/>
  <path d="M380,384 C386,340 380,310 362,292 L344,318 C352,340 362,364 380,384 Z" fill="{HAIR_DEEP}" opacity="0.55"/>"""

BODY = f"""
  <!-- body: hoodie -->
  <path d="M150,512 C146,448 164,388 214,366 L298,366 C348,388 366,448 362,512 Z" fill="url(#hoodie)"/>
  <path d="M214,366 C228,392 284,392 298,366 C290,386 270,398 256,398 C242,398 222,386 214,366 Z" fill="{HOODIE_SHADE}"/>
  <!-- hood edge -->
  <path d="M196,372 C206,356 230,350 256,350 C282,350 306,356 316,372 C300,366 280,364 256,364 C232,364 212,366 196,372 Z" fill="{HOODIE_SHADE}"/>
  <!-- drawstrings -->
  <path d="M240,392 L236,440" stroke="#ffffff" stroke-width="4" stroke-linecap="round"/>
  <path d="M272,392 L276,440" stroke="#ffffff" stroke-width="4" stroke-linecap="round"/>
  <circle cx="236" cy="444" r="5" fill="{GLOW}"/>
  <circle cx="276" cy="444" r="5" fill="{GLOW}"/>
  <!-- Flux emblem -->
  <path d="M262,450 L244,482 L258,482 L250,506 L276,470 L262,470 L270,450 Z" fill="{GLOW}"/>
  <!-- neck -->
  <path d="M236,330 L276,330 L278,362 C266,370 246,370 234,362 Z" fill="{SKIN_SHADE}"/>"""

FACE = f"""
  <!-- face -->
  <path d="M150,218 C150,150 198,112 256,112 C314,112 362,150 362,218 C362,282 318,332 256,336
           C194,332 150,282 150,218 Z" fill="{SKIN}"/>
  <ellipse cx="190" cy="276" rx="26" ry="16" fill="url(#blush)"/>
  <ellipse cx="322" cy="276" rx="26" ry="16" fill="url(#blush)"/>"""


def eye(cx: float, flip: bool) -> str:
    s = -1 if flip else 1
    return f"""
  <g>
    <ellipse cx="{cx}" cy="240" rx="32" ry="38" fill="#ffffff"/>
    <ellipse cx="{cx + 2 * s}" cy="242" rx="26" ry="33" fill="url(#iris)"/>
    <ellipse cx="{cx + 2 * s}" cy="246" rx="12" ry="17" fill="{LINE}"/>
    <ellipse cx="{cx + 2 * s}" cy="262" rx="15" ry="7" fill="{GLOW}" opacity="0.55"/>
    <circle cx="{cx - 9 * s}" cy="228" r="10" fill="#ffffff"/>
    <circle cx="{cx + 11 * s}" cy="256" r="4.5" fill="#ffffff"/>
    <path d="M{cx - 32},232 C{cx - 26},200 {cx + 26},200 {cx + 32},232" fill="none" stroke="{LINE}" stroke-width="6" stroke-linecap="round"/>
    <path d="M{cx + 30 * s},226 L{cx + 40 * s},220" stroke="{LINE}" stroke-width="4.5" stroke-linecap="round"/>
    <path d="M{cx - 16},180 C{cx - 6},174 {cx + 6},174 {cx + 16},180" fill="none" stroke="{HAIR_DEEP}" stroke-width="4" stroke-linecap="round" opacity="0.8"/>
  </g>"""


def closed_eye(cx: float) -> str:
    return f"""
  <path d="M{cx - 28},246 C{cx - 16},262 {cx + 16},262 {cx + 28},246" fill="none" stroke="{LINE}"
        stroke-width="6" stroke-linecap="round"/>
  <path d="M{cx - 26},252 L{cx - 34},260 M{cx + 26},252 L{cx + 34},260" stroke="{LINE}" stroke-width="4" stroke-linecap="round"/>"""


FRONT_HAIR = f"""
  <!-- bangs -->
  <path d="M138,222 C132,132 190,84 256,84 C322,84 380,132 374,222
           C368,202 362,188 354,178 Q352,200 344,212 Q340,176 326,152 Q322,188 306,206
           Q300,168 286,144 Q280,180 262,198 Q256,170 242,146 Q236,182 220,204
           Q214,174 202,154 Q194,192 180,212 Q174,192 166,180 C156,192 146,206 138,222 Z" fill="url(#hair)"/>
  <path d="M286,144 Q280,180 262,198 Q276,176 280,152 Z M242,146 Q236,182 220,204 Q234,184 238,160 Z" fill="{HAIR_SHADE}" opacity="0.6"/>
  <!-- cyan streak -->
  <path d="M326,150 L306,206 L318,160 C330,134 346,128 356,132 C344,138 334,142 326,150 Z" fill="{STREAK}"/>
  <!-- side locks -->
  <path d="M146,208 C138,262 144,316 170,352 C164,318 168,280 176,248 Z" fill="url(#hair)"/>
  <path d="M366,208 C374,262 368,316 342,352 C348,318 344,280 336,248 Z" fill="url(#hair)"/>
  <!-- ahoge -->
  <path d="M252,86 C236,56 248,30 276,26 C262,38 258,56 266,84 Z" fill="url(#hair)"/>
  <!-- lightning clip -->
  <path d="M196,160 L180,190 L194,190 L186,214 L212,178 L198,178 L206,160 Z" fill="#ffd166" stroke="{LINE}" stroke-width="2.5" stroke-linejoin="round"/>"""

PHONES_SVG = f"""
  <!-- headphones -->
  <path d="M128,214 C120,118 186,58 256,58 C326,58 392,118 384,214" fill="none" stroke="{PHONES}"
        stroke-width="16" stroke-linecap="round"/>
  <rect x="104" y="188" width="44" height="78" rx="20" fill="{PHONES}"/>
  <rect x="364" y="188" width="44" height="78" rx="20" fill="{PHONES}"/>
  <rect x="112" y="200" width="10" height="54" rx="5" fill="{GLOW}"/>
  <rect x="390" y="200" width="10" height="54" rx="5" fill="{GLOW}"/>"""


def happy() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">{DEFS}
  <g filter="url(#soft)">{BACK_HAIR}{BODY}{FACE}{eye(208, False)}{eye(304, True)}
  <!-- open smile -->
  <path d="M234,286 C240,310 272,310 278,286 Z" fill="#c23a62"/>
  <path d="M244,300 C250,308 264,308 270,300 C262,296 252,296 244,300 Z" fill="#ff8fa3"/>
  {FRONT_HAIR}{PHONES_SVG}
  <!-- waving hand -->
  <path d="M318,382 C348,378 372,362 384,338 L414,352 C402,386 372,414 334,424 Z" fill="url(#hoodie)"/>
  <path d="M384,338 L414,352 L410,362 L380,348 Z" fill="{HOODIE_SHADE}"/>
  <ellipse cx="400" cy="322" rx="22" ry="24" fill="{SKIN}"/>
  <path d="M386,308 L382,288 M396,302 L394,280 M406,302 L408,281 M416,310 L422,292" fill="none" stroke="{SKIN}"
        stroke-width="9" stroke-linecap="round"/>
  <path d="M380,324 C372,318 368,310 372,304" fill="none" stroke="{SKIN}" stroke-width="9" stroke-linecap="round"/>
  </g>
  <!-- sparkles -->
  <path d="M446,150 L452,168 L470,174 L452,180 L446,198 L440,180 L422,174 L440,168 Z" fill="#ffd166"/>
  <path d="M70,120 L74,132 L86,136 L74,140 L70,152 L66,140 L54,136 L66,132 Z" fill="{GLOW}"/>
</svg>
"""


def sleeping() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">{DEFS}
  <g filter="url(#soft)" transform="rotate(-8 256 300)">{BACK_HAIR}{BODY}{FACE}{closed_eye(208)}{closed_eye(304)}
  <!-- sleepy mouth -->
  <ellipse cx="256" cy="296" rx="8" ry="6" fill="#c23a62"/>
  {FRONT_HAIR}{PHONES_SVG}
  <!-- pillow hug -->
  <rect x="150" y="420" width="212" height="92" rx="40" fill="#f1edff"/>
  <path d="M170,440 C220,428 292,428 342,440" fill="none" stroke="#d9d2ff" stroke-width="6" stroke-linecap="round"/>
  <ellipse cx="170" cy="438" rx="22" ry="18" fill="{SKIN}"/>
  <ellipse cx="342" cy="438" rx="22" ry="18" fill="{SKIN}"/>
  </g>
  <!-- Zzz -->
  <g fill="{HAIR_DEEP}" font-family="Google Sans Flex, sans-serif" font-weight="800">
    <text x="386" y="132" font-size="52">Z</text>
    <text x="432" y="92" font-size="38" opacity="0.8">z</text>
    <text x="462" y="62" font-size="28" opacity="0.6">z</text>
  </g>
</svg>
"""


(HERE / "flux_happy.svg").write_text(happy())
(HERE / "flux_sleeping.svg").write_text(sleeping())
print("wrote", HERE / "flux_happy.svg", HERE / "flux_sleeping.svg")
