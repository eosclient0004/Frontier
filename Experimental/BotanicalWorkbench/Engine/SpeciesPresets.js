/*============================================================================================================================================
                                                           SPECIESPRESETS.JS
============================================================================================================================================*/
// 🧩 All 37 Weber & Penn botanical presets extracted verbatim from unassignedinbox/Slate (arena/01a096c9-slate)

export const CrownEnvelope = Object.freeze({
  Conical: 0,
  Spherical: 1,
  Hemispherical: 2,
  Cylindrical: 3,
  TaperedCylindrical: 4,
  Flame: 5,
  InverseConical: 6,
  TendFlame: 7,
});

export const CrownEnvelopeLabels = Object.freeze([
  'Conical',
  'Spherical',
  'Hemispherical',
  'Cylindrical',
  'Tapered Cylindrical',
  'Flame',
  'Inverse Conical',
  'Tend Flame',
]);

export const SpeciesGroups = [
  {
    "label": "Oaks",
    "names": [
      "English Oak",
      "White Oak",
      "Northern Red Oak",
      "Southern Live Oak",
      "Valley Oak",
      "Pin Oak",
      "Cork Oak",
      "California Black Oak"
    ]
  },
  {
    "label": "Forest · broadleaf",
    "names": [
      "European Beech",
      "Sugar Maple",
      "Quaking Aspen",
      "Black Tupelo",
      "Silver Birch",
      "Weeping Willow",
      "Japanese Maple"
    ]
  },
  {
    "label": "Forest · conifers",
    "names": [
      "Scots Pine",
      "Douglas Fir",
      "Coast Redwood",
      "Norway Spruce"
    ]
  },
  {
    "label": "Jungle",
    "names": [
      "Kapok",
      "Rain-Forest Fig",
      "Bamboo"
    ]
  },
  {
    "label": "Savanna",
    "names": [
      "Umbrella Thorn",
      "Baobab",
      "Marula",
      "Fever Tree"
    ]
  },
  {
    "label": "Desert",
    "names": [
      "Joshua Tree",
      "Desert Ironwood",
      "Saguaro Cactus",
      "Golden Barrel Cactus",
      "Organ Pipe Cactus",
      "Agave Americana",
      "Aloe Vera",
      "Ocotillo",
      "Dasylirion Sotol"
    ]
  },
  {
    "label": "Rocky terrain",
    "names": [
      "Bristlecone Pine",
      "Rowan"
    ]
  }
];

export const SpeciesPresets = [
  {
    "name": "English Oak",
    "botany": {
      "shape": 2,
      "scale": 16,
      "scaleV": 2,
      "levels": 4,
      "ratio": 0.022,
      "ratioPower": 1.35,
      "flare": 1.1,
      "baseSplits": 0,
      "baseSize": [
        0.22,
        0.05,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        10,
        6,
        3
      ],
      "curve": [
        0,
        25,
        10,
        0
      ],
      "curveBack": [
        0,
        -35,
        0,
        0
      ],
      "curveV": [
        60,
        140,
        120,
        80
      ],
      "bendV": [
        20,
        90,
        60,
        0
      ],
      "segSplits": [
        0.35,
        0.45,
        0.25,
        0
      ],
      "splitAngle": [
        38,
        40,
        40,
        0
      ],
      "splitAngleV": [
        8,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        48,
        45,
        45
      ],
      "downAngleV": [
        0,
        -30,
        20,
        20
      ],
      "rotate": [
        0,
        120,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        14,
        22,
        14
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.35,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 12,
      "leafScale": 0.14,
      "leafScaleX": 0.8,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.22,
      "lengthV": 0.06,
      "radius": 0.45,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#4a3c30",
      "barkDark": "#2a2018",
      "leaf": "#3d6b2e",
      "leafAlt": "#55843b",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Quaking Aspen",
    "botany": {
      "shape": 7,
      "scale": 13,
      "scaleV": 3,
      "levels": 3,
      "ratio": 0.015,
      "ratioPower": 1.2,
      "flare": 0.6,
      "baseSplits": 0,
      "baseSize": [
        0.3,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.3,
        0.6,
        0
      ],
      "lengthV": [
        0,
        0,
        0.1,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        6,
        5,
        4,
        1
      ],
      "curve": [
        0,
        -40,
        -60,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        20,
        100,
        100,
        0
      ],
      "bendV": [
        0,
        50,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        40,
        0,
        0,
        0
      ],
      "splitAngleV": [
        5,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        60,
        60,
        45
      ],
      "downAngleV": [
        0,
        -50,
        20,
        30
      ],
      "rotate": [
        0,
        140,
        140,
        77
      ],
      "rotateV": [
        0,
        0,
        0,
        0
      ],
      "branches": [
        1,
        45,
        26,
        1
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.5,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 30,
      "leafScale": 0.17,
      "leafScaleX": 1,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 4,
      "length": 0.16,
      "lengthV": 0.06,
      "radius": 0.38,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.3,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#cfcac0",
      "barkDark": "#59554d",
      "leaf": "#8eb334",
      "leafAlt": "#d4b82e",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Black Tupelo",
    "botany": {
      "shape": 4,
      "scale": 23,
      "scaleV": 5,
      "levels": 4,
      "ratio": 0.015,
      "ratioPower": 1.3,
      "flare": 1,
      "baseSplits": 0,
      "baseSize": [
        0.2,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.3,
        0.6,
        0.2
      ],
      "lengthV": [
        0,
        0.05,
        0.1,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        10,
        8,
        2
      ],
      "curve": [
        0,
        0,
        -10,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        40,
        90,
        150,
        0
      ],
      "bendV": [
        0,
        100,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        40,
        0,
        0,
        0
      ],
      "splitAngleV": [
        5,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        60,
        40,
        45
      ],
      "downAngleV": [
        0,
        -40,
        10,
        10
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        60,
        50,
        0
      ],
      "branches": [
        1,
        55,
        20,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.5,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 14,
      "leafScale": 0.2,
      "leafScaleX": 1,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.16,
      "lengthV": 0.06,
      "radius": 0.4,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#4c4640",
      "barkDark": "#2a2520",
      "leaf": "#4d7330",
      "leafAlt": "#9e3824",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "California Black Oak",
    "botany": {
      "shape": 2,
      "scale": 10,
      "scaleV": 2,
      "levels": 3,
      "ratio": 0.018,
      "ratioPower": 1.25,
      "flare": 1.2,
      "baseSplits": 0,
      "baseSize": [
        0.05,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.8,
        0.3,
        0.4
      ],
      "lengthV": [
        0,
        0.1,
        0.05,
        0
      ],
      "taper": [
        0.95,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        10,
        4,
        1
      ],
      "curve": [
        0,
        40,
        0,
        0
      ],
      "curveBack": [
        0,
        -70,
        0,
        0
      ],
      "curveV": [
        90,
        150,
        30,
        0
      ],
      "bendV": [
        0,
        100,
        0,
        0
      ],
      "segSplits": [
        0.4,
        0.2,
        0.1,
        0
      ],
      "splitAngle": [
        10,
        10,
        10,
        0
      ],
      "splitAngleV": [
        0,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        30,
        45,
        45
      ],
      "downAngleV": [
        0,
        -30,
        10,
        10
      ],
      "rotate": [
        0,
        80,
        140,
        140
      ],
      "rotateV": [
        0,
        20,
        20,
        20
      ],
      "branches": [
        1,
        24,
        60,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.8,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 20,
      "leafScale": 0.2,
      "leafScaleX": 0.66,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.26,
      "lengthV": 0.06,
      "radius": 0.46,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#39322c",
      "barkDark": "#1e1915",
      "leaf": "#426b2f",
      "leafAlt": "#658842",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Southern Live Oak",
    "botany": {
      "shape": 2,
      "scale": 11,
      "scaleV": 2,
      "levels": 4,
      "ratio": 0.036,
      "ratioPower": 1.1,
      "flare": 1,
      "baseSplits": 3,
      "baseSize": [
        0.22,
        0.05,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.9,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.1,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        12,
        6,
        3
      ],
      "curve": [
        0,
        20,
        15,
        0
      ],
      "curveBack": [
        0,
        -35,
        0,
        0
      ],
      "curveV": [
        60,
        120,
        120,
        80
      ],
      "bendV": [
        20,
        80,
        60,
        0
      ],
      "segSplits": [
        0.3,
        0.35,
        0.2,
        0
      ],
      "splitAngle": [
        70,
        45,
        40,
        0
      ],
      "splitAngleV": [
        10,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        72,
        48,
        45
      ],
      "downAngleV": [
        0,
        -18,
        20,
        20
      ],
      "rotate": [
        0,
        120,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        10,
        22,
        14
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.2,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 14,
      "leafScale": 0.08,
      "leafScaleX": 0.5,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 28,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 7,
      "length": 0.24,
      "lengthV": 0.06,
      "radius": 0.45,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#524940",
      "barkDark": "#302922",
      "leaf": "#2f5829",
      "leafAlt": "#456f38",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "White Oak",
    "botany": {
      "shape": 2,
      "scale": 17,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.028,
      "ratioPower": 1.15,
      "flare": 1,
      "baseSplits": 2,
      "baseSize": [
        0.2,
        0.05,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.65,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        9,
        9,
        6,
        3
      ],
      "curve": [
        0,
        15,
        5,
        0
      ],
      "curveBack": [
        0,
        -25,
        0,
        0
      ],
      "curveV": [
        50,
        160,
        140,
        90
      ],
      "bendV": [
        15,
        100,
        70,
        0
      ],
      "segSplits": [
        0.4,
        0.4,
        0.2,
        0
      ],
      "splitAngle": [
        48,
        42,
        40,
        0
      ],
      "splitAngleV": [
        8,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        62,
        50,
        45
      ],
      "downAngleV": [
        0,
        -25,
        20,
        20
      ],
      "rotate": [
        0,
        125,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        13,
        24,
        14
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.3,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 12,
      "leafScale": 0.16,
      "leafScaleX": 0.6,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 28,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.24,
      "lengthV": 0.06,
      "radius": 0.46,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#6d655b",
      "barkDark": "#423c35",
      "leaf": "#457034",
      "leafAlt": "#648d48",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Northern Red Oak",
    "botany": {
      "shape": 1,
      "scale": 26,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.02,
      "ratioPower": 1.3,
      "flare": 0.8,
      "baseSplits": 0,
      "baseSize": [
        0.35,
        0.04,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.06,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        8,
        6,
        3
      ],
      "curve": [
        0,
        -15,
        5,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        40,
        80,
        100,
        80
      ],
      "bendV": [
        10,
        60,
        40,
        0
      ],
      "segSplits": [
        0.25,
        0.3,
        0.2,
        0
      ],
      "splitAngle": [
        30,
        35,
        40,
        0
      ],
      "splitAngleV": [
        6,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        42,
        45,
        45
      ],
      "downAngleV": [
        0,
        -20,
        15,
        15
      ],
      "rotate": [
        0,
        130,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        13,
        24,
        14
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.5,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 12,
      "leafScale": 0.18,
      "leafScaleX": 0.7,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.2,
      "lengthV": 0.06,
      "radius": 0.42,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#4f4842",
      "barkDark": "#2c2723",
      "leaf": "#4a6930",
      "leafAlt": "#7a4526",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Valley Oak",
    "botany": {
      "shape": 2,
      "scale": 22,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.03,
      "ratioPower": 1.2,
      "flare": 1.2,
      "baseSplits": 0,
      "baseSize": [
        0.2,
        0.05,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.6,
        0.45,
        0.35
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        10,
        8,
        4
      ],
      "curve": [
        0,
        20,
        30,
        20
      ],
      "curveBack": [
        0,
        -20,
        0,
        0
      ],
      "curveV": [
        50,
        120,
        100,
        60
      ],
      "bendV": [
        15,
        80,
        40,
        0
      ],
      "segSplits": [
        0.5,
        0.35,
        0.15,
        0
      ],
      "splitAngle": [
        45,
        40,
        35,
        0
      ],
      "splitAngleV": [
        8,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        50,
        50,
        40
      ],
      "downAngleV": [
        0,
        -25,
        15,
        15
      ],
      "rotate": [
        0,
        130,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        12,
        22,
        16
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": -1,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 14,
      "leafScale": 0.12,
      "leafScaleX": 0.7,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 28,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 7,
      "length": 0.24,
      "lengthV": 0.06,
      "radius": 0.48,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#675e52",
      "barkDark": "#3e3830",
      "leaf": "#4f7337",
      "leafAlt": "#6b8e49",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Pin Oak",
    "botany": {
      "shape": 0,
      "scale": 20,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.019,
      "ratioPower": 1.4,
      "flare": 0.4,
      "baseSplits": 0,
      "baseSize": [
        0.15,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.34,
        0.5,
        0.3
      ],
      "lengthV": [
        0,
        0.04,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        12,
        8,
        5,
        2
      ],
      "curve": [
        0,
        10,
        5,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        15,
        40,
        80,
        60
      ],
      "bendV": [
        0,
        20,
        20,
        0
      ],
      "segSplits": [
        0,
        0,
        0.1,
        0
      ],
      "splitAngle": [
        0,
        0,
        30,
        0
      ],
      "splitAngleV": [
        0,
        0,
        8,
        0
      ],
      "downAngle": [
        0,
        80,
        60,
        45
      ],
      "downAngleV": [
        0,
        -45,
        15,
        15
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        20,
        20,
        20
      ],
      "branches": [
        1,
        40,
        22,
        8
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.4,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 16,
      "leafScale": 0.12,
      "leafScaleX": 0.7,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.16,
      "lengthV": 0.06,
      "radius": 0.36,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#4a433c",
      "barkDark": "#2c2620",
      "leaf": "#3d692c",
      "leafAlt": "#5a833c",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Cork Oak",
    "botany": {
      "shape": 2,
      "scale": 11,
      "scaleV": 2,
      "levels": 4,
      "ratio": 0.04,
      "ratioPower": 1.1,
      "flare": 0.9,
      "baseSplits": 2,
      "baseSize": [
        0.22,
        0.08,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.6,
        0.4,
        0.3
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        8,
        6,
        3
      ],
      "curve": [
        10,
        20,
        10,
        0
      ],
      "curveBack": [
        -15,
        -30,
        0,
        0
      ],
      "curveV": [
        150,
        220,
        160,
        100
      ],
      "bendV": [
        40,
        120,
        80,
        0
      ],
      "segSplits": [
        0.4,
        0.35,
        0.2,
        0
      ],
      "splitAngle": [
        50,
        45,
        40,
        0
      ],
      "splitAngleV": [
        10,
        12,
        10,
        0
      ],
      "downAngle": [
        0,
        55,
        50,
        45
      ],
      "downAngleV": [
        0,
        -25,
        20,
        20
      ],
      "rotate": [
        0,
        120,
        140,
        140
      ],
      "rotateV": [
        0,
        40,
        40,
        20
      ],
      "branches": [
        1,
        9,
        16,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.4,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 14,
      "leafScale": 0.07,
      "leafScaleX": 0.5,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 28,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.22,
      "lengthV": 0.06,
      "radius": 0.44,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#736556",
      "barkDark": "#43382e",
      "leaf": "#3b5c34",
      "leafAlt": "#547349",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Silver Birch",
    "botany": {
      "shape": 3,
      "scale": 18,
      "scaleV": 4,
      "levels": 3,
      "ratio": 0.013,
      "ratioPower": 1.5,
      "flare": 0.5,
      "baseSplits": 0,
      "baseSize": [
        0.3,
        0.1,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.3,
        0.4,
        0
      ],
      "lengthV": [
        0,
        0.05,
        0.2,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        8,
        6,
        0
      ],
      "curve": [
        0,
        0,
        -10,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        50,
        150,
        200,
        0
      ],
      "bendV": [
        0,
        100,
        0,
        0
      ],
      "segSplits": [
        0,
        0.3,
        0,
        0
      ],
      "splitAngle": [
        15,
        10,
        0,
        0
      ],
      "splitAngleV": [
        5,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        50,
        40,
        45
      ],
      "downAngleV": [
        0,
        -20,
        10,
        10
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        60,
        50,
        0
      ],
      "branches": [
        1,
        28,
        40,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": -2,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 40,
      "leafScale": 0.15,
      "leafScaleX": 1,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 4,
      "length": 0.14,
      "lengthV": 0.06,
      "radius": 0.36,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.3,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#dedad2",
      "barkDark": "#36332e",
      "leaf": "#5c8c38",
      "leafAlt": "#7eab4a",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Weeping Willow",
    "botany": {
      "shape": 4,
      "scale": 15,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.03,
      "ratioPower": 1.2,
      "flare": 0.75,
      "baseSplits": 2,
      "baseSize": [
        0.05,
        0.3,
        0.05,
        0.05
      ],
      "length": [
        1,
        0.35,
        2,
        0.1
      ],
      "lengthV": [
        0,
        0.1,
        0,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        12,
        12,
        2
      ],
      "curve": [
        0,
        40,
        0,
        0
      ],
      "curveBack": [
        25,
        0,
        0,
        0
      ],
      "curveV": [
        90,
        200,
        0,
        0
      ],
      "bendV": [
        0,
        160,
        0,
        0
      ],
      "segSplits": [
        0.2,
        0.2,
        0.1,
        0
      ],
      "splitAngle": [
        40,
        30,
        45,
        0
      ],
      "splitAngleV": [
        5,
        10,
        20,
        0
      ],
      "downAngle": [
        0,
        40,
        30,
        20
      ],
      "downAngleV": [
        0,
        10,
        10,
        10
      ],
      "rotate": [
        0,
        -120,
        -120,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        0
      ],
      "branches": [
        1,
        14,
        18,
        40
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        0.1,
        1
      ],
      "attractionUp": -3,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 15,
      "leafScale": 0.13,
      "leafScaleX": 0.2,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.22,
      "lengthV": 0.06,
      "radius": 0.42,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#564c3d",
      "barkDark": "#342c21",
      "leaf": "#5c8a3a",
      "leafAlt": "#7ea850",
      "leafStyle": "willow"
    }
  },
  {
    "name": "Scots Pine",
    "botany": {
      "shape": 0,
      "scale": 22,
      "scaleV": 4,
      "levels": 3,
      "ratio": 0.014,
      "ratioPower": 1.4,
      "flare": 0.4,
      "baseSplits": 0,
      "baseSize": [
        0.45,
        0.05,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.28,
        0.35,
        0
      ],
      "lengthV": [
        0,
        0.06,
        0.1,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        6,
        4,
        1
      ],
      "curve": [
        0,
        -25,
        0,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        30,
        60,
        90,
        0
      ],
      "bendV": [
        0,
        40,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        40,
        0,
        0,
        0
      ],
      "splitAngleV": [
        5,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        70,
        55,
        45
      ],
      "downAngleV": [
        0,
        -30,
        20,
        10
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        25,
        30,
        0
      ],
      "branches": [
        1,
        42,
        24,
        0
      ],
      "branchDist": [
        0,
        3,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 1.5,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 60,
      "leafScale": 0.1,
      "leafScaleX": 0.25,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 4,
      "length": 0.13,
      "lengthV": 0.06,
      "radius": 0.4,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.25,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#7d4d32",
      "barkDark": "#472c1d",
      "leaf": "#2d5736",
      "leafAlt": "#3f6e46",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Coast Redwood",
    "botany": {
      "shape": 4,
      "scale": 70,
      "scaleV": 15,
      "levels": 3,
      "ratio": 0.017,
      "ratioPower": 1.5,
      "flare": 1.6,
      "baseSplits": 0,
      "baseSize": [
        0.4,
        0.05,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.12,
        0.45,
        0
      ],
      "lengthV": [
        0,
        0.04,
        0.1,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        12,
        5,
        4,
        1
      ],
      "curve": [
        0,
        -12,
        0,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        8,
        40,
        80,
        0
      ],
      "bendV": [
        0,
        30,
        20,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        0,
        0,
        0,
        0
      ],
      "splitAngleV": [
        0,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        80,
        50,
        45
      ],
      "downAngleV": [
        0,
        -25,
        15,
        10
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        0
      ],
      "branches": [
        1,
        90,
        26,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.85,
        1,
        1
      ],
      "attractionUp": 0.8,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 40,
      "leafScale": 0.14,
      "leafScaleX": 0.35,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0.5,
      "rootLobeHeight": 0.06
    },
    "roots": {
      "enabled": true,
      "count": 7,
      "length": 0.09,
      "lengthV": 0.06,
      "radius": 0.36,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.7,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.4,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#6f3824",
      "barkDark": "#3e1e13",
      "leaf": "#2b5431",
      "leafAlt": "#3d6941",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Norway Spruce",
    "botany": {
      "shape": 0,
      "scale": 32,
      "scaleV": 6,
      "levels": 3,
      "ratio": 0.012,
      "ratioPower": 1.6,
      "flare": 0.5,
      "baseSplits": 0,
      "baseSize": [
        0.12,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.24,
        0.3,
        0
      ],
      "lengthV": [
        0,
        0.04,
        0.08,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        6,
        4,
        1
      ],
      "curve": [
        0,
        22,
        -25,
        0
      ],
      "curveBack": [
        0,
        -55,
        0,
        0
      ],
      "curveV": [
        6,
        30,
        60,
        0
      ],
      "bendV": [
        0,
        15,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        0,
        0,
        0,
        0
      ],
      "splitAngleV": [
        0,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        78,
        65,
        45
      ],
      "downAngleV": [
        0,
        -20,
        20,
        10
      ],
      "rotate": [
        0,
        0,
        140,
        140
      ],
      "rotateV": [
        0,
        20,
        20,
        0
      ],
      "branches": [
        1,
        170,
        26,
        0
      ],
      "branchDist": [
        0,
        5,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.9,
        1,
        1
      ],
      "attractionUp": -1.6,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 60,
      "leafScale": 0.09,
      "leafScaleX": 0.3,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.14,
      "lengthV": 0.06,
      "radius": 0.36,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.3,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#5c3f30",
      "barkDark": "#322018",
      "leaf": "#234a2d",
      "leafAlt": "#325f3b",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Bamboo",
    "botany": {
      "shape": 3,
      "scale": 11,
      "scaleV": 3,
      "levels": 3,
      "ratio": 0.0075,
      "ratioPower": 1,
      "flare": 0.05,
      "baseSplits": 0,
      "baseSize": [
        0.45,
        0.1,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.14,
        0.4,
        0
      ],
      "lengthV": [
        0,
        0.05,
        0.1,
        0
      ],
      "taper": [
        0.6,
        1,
        1,
        1
      ],
      "curveRes": [
        14,
        4,
        3,
        1
      ],
      "curve": [
        0,
        15,
        10,
        0
      ],
      "curveBack": [
        45,
        0,
        0,
        0
      ],
      "curveV": [
        10,
        30,
        40,
        0
      ],
      "bendV": [
        0,
        10,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        0,
        0,
        0,
        0
      ],
      "splitAngleV": [
        0,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        42,
        40,
        40
      ],
      "downAngleV": [
        0,
        10,
        10,
        10
      ],
      "rotate": [
        0,
        0,
        140,
        140
      ],
      "rotateV": [
        0,
        25,
        20,
        0
      ],
      "branches": [
        1,
        36,
        8,
        0
      ],
      "branchDist": [
        0,
        2,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.7,
        1,
        1
      ],
      "attractionUp": 0.6,
      "flatten": 0,
      "nodeSwell": 0.12,
      "nodeSpacing": 0.045,
      "leaves": 30,
      "leafScale": 0.12,
      "leafScaleX": 0.15,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0,
      "rootLobeHeight": 0.02,
      "ringsPerSegment": [
        2,
        2,
        2,
        1
      ]
    },
    "roots": {
      "enabled": false,
      "count": 0,
      "length": 0.05,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 0,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#6b873c",
      "barkDark": "#435823",
      "leaf": "#4f8231",
      "leafAlt": "#6ea145",
      "leafStyle": "willow"
    }
  },
  {
    "name": "Umbrella Thorn",
    "botany": {
      "shape": 6,
      "scale": 8,
      "scaleV": 1.5,
      "levels": 4,
      "ratio": 0.032,
      "ratioPower": 1.2,
      "flare": 0.7,
      "baseSplits": 2,
      "baseSize": [
        0.3,
        0.1,
        0.05,
        0.02
      ],
      "length": [
        1,
        0.85,
        0.5,
        0.3
      ],
      "lengthV": [
        0,
        0.1,
        0.1,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        6,
        8,
        6,
        3
      ],
      "curve": [
        0,
        5,
        15,
        5
      ],
      "curveBack": [
        0,
        45,
        0,
        0
      ],
      "curveV": [
        40,
        80,
        100,
        80
      ],
      "bendV": [
        0,
        50,
        60,
        20
      ],
      "segSplits": [
        0.6,
        0.55,
        0.3,
        0
      ],
      "splitAngle": [
        60,
        42,
        40,
        0
      ],
      "splitAngleV": [
        10,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        62,
        65,
        55
      ],
      "downAngleV": [
        0,
        -20,
        15,
        15
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        40,
        40,
        0
      ],
      "branches": [
        1,
        20,
        16,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": -0.3,
      "flatten": 0.9,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 20,
      "leafScale": 0.08,
      "leafScaleX": 0.5,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.24,
      "lengthV": 0.06,
      "radius": 0.42,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#594b3b",
      "barkDark": "#33291f",
      "leaf": "#526e32",
      "leafAlt": "#6e8a43",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Baobab",
    "botany": {
      "shape": 2,
      "scale": 16,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.15,
      "ratioPower": 1,
      "flare": 0.25,
      "baseSplits": 0,
      "baseSize": [
        0.6,
        0.15,
        0.05,
        0.02
      ],
      "length": [
        1,
        0.34,
        0.5,
        0.4
      ],
      "lengthV": [
        0,
        0.08,
        0.1,
        0.05
      ],
      "taper": [
        0.72,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        6,
        5,
        3
      ],
      "curve": [
        0,
        15,
        20,
        10
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        15,
        120,
        140,
        100
      ],
      "bendV": [
        0,
        40,
        50,
        20
      ],
      "segSplits": [
        0,
        0.5,
        0.4,
        0
      ],
      "splitAngle": [
        0,
        45,
        45,
        0
      ],
      "splitAngleV": [
        0,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        42,
        50,
        50
      ],
      "downAngleV": [
        0,
        -20,
        15,
        15
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        40,
        40,
        0
      ],
      "branches": [
        1,
        16,
        10,
        6
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.32,
        1.1,
        1.3
      ],
      "attractionUp": 0.9,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 10,
      "leafScale": 0.14,
      "leafScaleX": 0.6,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 32,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0.2,
      "rootLobeHeight": 0.05
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.3,
      "lengthV": 0.06,
      "radius": 0.13,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.5,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.4,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#827164",
      "barkDark": "#52453b",
      "leaf": "#4b6e30",
      "leafAlt": "#668742",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "European Beech",
    "botany": {
      "shape": 2,
      "scale": 24,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.02,
      "ratioPower": 1.25,
      "flare": 0.9,
      "baseSplits": 0,
      "baseSize": [
        0.2,
        0.04,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.5,
        0.35
      ],
      "lengthV": [
        0,
        0.06,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        8,
        6,
        3
      ],
      "curve": [
        0,
        0,
        10,
        10
      ],
      "curveBack": [
        0,
        -20,
        0,
        0
      ],
      "curveV": [
        30,
        60,
        100,
        80
      ],
      "bendV": [
        10,
        40,
        40,
        0
      ],
      "segSplits": [
        0.3,
        0.3,
        0.2,
        0
      ],
      "splitAngle": [
        35,
        35,
        35,
        0
      ],
      "splitAngleV": [
        6,
        8,
        8,
        0
      ],
      "downAngle": [
        0,
        48,
        50,
        45
      ],
      "downAngleV": [
        0,
        -22,
        15,
        15
      ],
      "rotate": [
        0,
        130,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        16,
        22,
        16
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": -0.4,
      "flatten": 0.2,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 22,
      "leafScale": 0.11,
      "leafScaleX": 0.7,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.22,
      "lengthV": 0.06,
      "radius": 0.42,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#7c7872",
      "barkDark": "#53504c",
      "leaf": "#4a7a2f",
      "leafAlt": "#6a9642",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Sugar Maple",
    "botany": {
      "shape": 1,
      "scale": 24,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.019,
      "ratioPower": 1.3,
      "flare": 0.7,
      "baseSplits": 0,
      "baseSize": [
        0.2,
        0.04,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.5,
        0.5,
        0.35
      ],
      "lengthV": [
        0,
        0.06,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        10,
        8,
        6,
        3
      ],
      "curve": [
        0,
        -20,
        0,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        30,
        60,
        100,
        80
      ],
      "bendV": [
        10,
        40,
        40,
        0
      ],
      "segSplits": [
        0.35,
        0.3,
        0.2,
        0
      ],
      "splitAngle": [
        30,
        32,
        35,
        0
      ],
      "splitAngleV": [
        6,
        8,
        8,
        0
      ],
      "downAngle": [
        0,
        40,
        45,
        45
      ],
      "downAngleV": [
        0,
        -20,
        15,
        15
      ],
      "rotate": [
        0,
        137,
        140,
        140
      ],
      "rotateV": [
        0,
        25,
        30,
        20
      ],
      "branches": [
        1,
        16,
        22,
        16
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.6,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 20,
      "leafScale": 0.15,
      "leafScaleX": 0.9,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.2,
      "lengthV": 0.06,
      "radius": 0.42,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#5a524a",
      "barkDark": "#352f29",
      "leaf": "#b84c1e",
      "leafAlt": "#d87a24",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Douglas Fir",
    "botany": {
      "shape": 0,
      "scale": 55,
      "scaleV": 8,
      "levels": 3,
      "ratio": 0.012,
      "ratioPower": 1.5,
      "flare": 0.9,
      "baseSplits": 0,
      "baseSize": [
        0.4,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.2,
        0.32,
        0
      ],
      "lengthV": [
        0,
        0.03,
        0.06,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        12,
        6,
        4,
        1
      ],
      "curve": [
        0,
        8,
        -20,
        0
      ],
      "curveBack": [
        0,
        -15,
        0,
        0
      ],
      "curveV": [
        6,
        30,
        50,
        0
      ],
      "bendV": [
        0,
        15,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        0,
        0,
        0,
        0
      ],
      "splitAngleV": [
        0,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        80,
        60,
        45
      ],
      "downAngleV": [
        0,
        -18,
        20,
        10
      ],
      "rotate": [
        0,
        140,
        140,
        77
      ],
      "rotateV": [
        0,
        25,
        40,
        0
      ],
      "branches": [
        1,
        130,
        24,
        0
      ],
      "branchDist": [
        0,
        4,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.85,
        1,
        1
      ],
      "attractionUp": -1.2,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 30,
      "leafScale": 0.05,
      "leafScaleX": 0.15,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.12,
      "lengthV": 0.06,
      "radius": 0.4,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.4,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#543b2c",
      "barkDark": "#2c1e16",
      "leaf": "#264f30",
      "leafAlt": "#366640",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Kapok",
    "botany": {
      "shape": 6,
      "scale": 50,
      "scaleV": 6,
      "levels": 4,
      "ratio": 0.022,
      "ratioPower": 1.1,
      "flare": 2.4,
      "baseSplits": 0,
      "baseSize": [
        0.62,
        0.05,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.5,
        0.35
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        12,
        10,
        6,
        3
      ],
      "curve": [
        0,
        10,
        10,
        5
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        15,
        60,
        80,
        60
      ],
      "bendV": [
        5,
        40,
        40,
        0
      ],
      "segSplits": [
        0,
        0.35,
        0.25,
        0
      ],
      "splitAngle": [
        0,
        40,
        40,
        0
      ],
      "splitAngleV": [
        0,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        85,
        70,
        55
      ],
      "downAngleV": [
        0,
        -10,
        15,
        15
      ],
      "rotate": [
        0,
        120,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        8,
        18,
        14
      ],
      "branchDist": [
        0,
        3,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": -0.2,
      "flatten": 0.8,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 16,
      "leafScale": 0.12,
      "leafScaleX": 0.5,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 40,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0.9,
      "rootLobeHeight": 0.12
    },
    "roots": {
      "enabled": true,
      "count": 7,
      "length": 0.2,
      "lengthV": 0.06,
      "radius": 0.5,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 1.6,
      "descent": 30,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.4,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#7a746a",
      "barkDark": "#4c4740",
      "leaf": "#376b2c",
      "leafAlt": "#4f853c",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Rain-Forest Fig",
    "botany": {
      "shape": 2,
      "scale": 28,
      "scaleV": 4,
      "levels": 4,
      "ratio": 0.03,
      "ratioPower": 1.15,
      "flare": 1.8,
      "baseSplits": 2,
      "baseSize": [
        0.3,
        0.06,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.7,
        0.5,
        0.35
      ],
      "lengthV": [
        0,
        0.1,
        0.1,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        10,
        6,
        3
      ],
      "curve": [
        0,
        20,
        15,
        5
      ],
      "curveBack": [
        0,
        -30,
        0,
        0
      ],
      "curveV": [
        60,
        160,
        140,
        90
      ],
      "bendV": [
        20,
        100,
        60,
        0
      ],
      "segSplits": [
        0.3,
        0.45,
        0.25,
        0
      ],
      "splitAngle": [
        45,
        45,
        40,
        0
      ],
      "splitAngleV": [
        10,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        60,
        55,
        50
      ],
      "downAngleV": [
        0,
        -20,
        20,
        20
      ],
      "rotate": [
        0,
        120,
        140,
        140
      ],
      "rotateV": [
        0,
        40,
        40,
        20
      ],
      "branches": [
        1,
        12,
        20,
        14
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": -0.2,
      "flatten": 0.4,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 18,
      "leafScale": 0.18,
      "leafScaleX": 0.6,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 32,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0.6,
      "rootLobeHeight": 0.1
    },
    "roots": {
      "enabled": true,
      "count": 8,
      "length": 0.3,
      "lengthV": 0.06,
      "radius": 0.5,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 1,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 5,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#685f52",
      "barkDark": "#3c362e",
      "leaf": "#2a5e29",
      "leafAlt": "#407838",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Marula",
    "botany": {
      "shape": 2,
      "scale": 12,
      "scaleV": 2,
      "levels": 4,
      "ratio": 0.034,
      "ratioPower": 1.15,
      "flare": 0.6,
      "baseSplits": 0,
      "baseSize": [
        0.4,
        0.06,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.6,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        8,
        6,
        3
      ],
      "curve": [
        0,
        15,
        10,
        5
      ],
      "curveBack": [
        0,
        -20,
        0,
        0
      ],
      "curveV": [
        30,
        120,
        120,
        80
      ],
      "bendV": [
        10,
        60,
        50,
        0
      ],
      "segSplits": [
        0.2,
        0.45,
        0.25,
        0
      ],
      "splitAngle": [
        40,
        45,
        40,
        0
      ],
      "splitAngleV": [
        8,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        45,
        50,
        45
      ],
      "downAngleV": [
        0,
        -20,
        20,
        20
      ],
      "rotate": [
        0,
        130,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        8,
        16,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.3,
      "flatten": 0.3,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 14,
      "leafScale": 0.1,
      "leafScaleX": 0.5,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.2,
      "lengthV": 0.06,
      "radius": 0.4,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#78685a",
      "barkDark": "#4a3f35",
      "leaf": "#4a7033",
      "leafAlt": "#688c48",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Fever Tree",
    "botany": {
      "shape": 6,
      "scale": 16,
      "scaleV": 3,
      "levels": 4,
      "ratio": 0.018,
      "ratioPower": 1.2,
      "flare": 0.5,
      "baseSplits": 0,
      "baseSize": [
        0.45,
        0.05,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.5,
        0.5,
        0.3
      ],
      "lengthV": [
        0,
        0.1,
        0.1,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        8,
        6,
        3
      ],
      "curve": [
        0,
        10,
        15,
        5
      ],
      "curveBack": [
        0,
        20,
        0,
        0
      ],
      "curveV": [
        30,
        80,
        100,
        80
      ],
      "bendV": [
        10,
        50,
        60,
        20
      ],
      "segSplits": [
        0.5,
        0.45,
        0.3,
        0
      ],
      "splitAngle": [
        40,
        40,
        40,
        0
      ],
      "splitAngleV": [
        10,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        55,
        60,
        55
      ],
      "downAngleV": [
        0,
        -20,
        15,
        15
      ],
      "rotate": [
        0,
        140,
        140,
        140
      ],
      "rotateV": [
        0,
        40,
        40,
        0
      ],
      "branches": [
        1,
        10,
        14,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.2,
      "flatten": 0.7,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 16,
      "leafScale": 0.07,
      "leafScaleX": 0.4,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.18,
      "lengthV": 0.06,
      "radius": 0.38,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#9fa356",
      "barkDark": "#696c34",
      "leaf": "#5c7d36",
      "leafAlt": "#7a994a",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Joshua Tree",
    "botany": {
      "shape": 1,
      "scale": 5.5,
      "scaleV": 1.5,
      "levels": 3,
      "ratio": 0.07,
      "ratioPower": 0.8,
      "flare": 0.5,
      "baseSplits": 2,
      "baseSize": [
        0.3,
        0.15,
        0.1,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.5,
        0
      ],
      "lengthV": [
        0,
        0.15,
        0.15,
        0
      ],
      "taper": [
        0.55,
        0.45,
        0.35,
        1
      ],
      "curveRes": [
        6,
        5,
        4,
        1
      ],
      "curve": [
        35,
        25,
        25,
        0
      ],
      "curveBack": [
        -45,
        -35,
        -35,
        0
      ],
      "curveV": [
        110,
        90,
        90,
        0
      ],
      "bendV": [
        70,
        40,
        40,
        0
      ],
      "segSplits": [
        0.7,
        0.9,
        0.9,
        0
      ],
      "splitAngle": [
        80,
        60,
        60,
        0
      ],
      "splitAngleV": [
        20,
        15,
        15,
        0
      ],
      "downAngle": [
        0,
        55,
        55,
        50
      ],
      "downAngleV": [
        0,
        10,
        10,
        40
      ],
      "rotate": [
        0,
        140,
        140,
        137
      ],
      "rotateV": [
        0,
        40,
        40,
        20
      ],
      "branches": [
        1,
        4,
        3,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.95,
        0.95,
        1
      ],
      "attractionUp": 1,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 110,
      "leafScale": 0.32,
      "leafScaleX": 0.12,
      "leafTuft": 0.2
    },
    "surface": {
      "trunkRadialSegments": 20,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0.15,
      "rootLobeHeight": 0.05
    },
    "roots": {
      "enabled": true,
      "count": 4,
      "length": 0.12,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.3,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#6e5c4a",
      "barkDark": "#403428",
      "leaf": "#4f6e3a",
      "leafAlt": "#6a874d",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Desert Ironwood",
    "botany": {
      "shape": 2,
      "scale": 7,
      "scaleV": 1.5,
      "levels": 4,
      "ratio": 0.035,
      "ratioPower": 1.1,
      "flare": 0.6,
      "baseSplits": 2,
      "baseSize": [
        0.15,
        0.06,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.7,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.1,
        0.1,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        6,
        8,
        6,
        3
      ],
      "curve": [
        10,
        20,
        10,
        0
      ],
      "curveBack": [
        -15,
        -30,
        0,
        0
      ],
      "curveV": [
        160,
        240,
        200,
        140
      ],
      "bendV": [
        40,
        140,
        100,
        0
      ],
      "segSplits": [
        0.4,
        0.4,
        0.25,
        0
      ],
      "splitAngle": [
        55,
        50,
        45,
        0
      ],
      "splitAngleV": [
        12,
        12,
        10,
        0
      ],
      "downAngle": [
        0,
        55,
        55,
        50
      ],
      "downAngleV": [
        0,
        -20,
        20,
        20
      ],
      "rotate": [
        0,
        120,
        140,
        140
      ],
      "rotateV": [
        0,
        40,
        40,
        20
      ],
      "branches": [
        1,
        8,
        14,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.3,
      "flatten": 0.2,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 14,
      "leafScale": 0.05,
      "leafScaleX": 0.4,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.2,
      "lengthV": 0.06,
      "radius": 0.36,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#635951",
      "barkDark": "#3a332d",
      "leaf": "#5c7349",
      "leafAlt": "#778c62",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Saguaro Cactus",
    "botany": {
      "shape": 4,
      "scale": 8.5,
      "scaleV": 1.2,
      "levels": 3,
      "ratio": 0.065,
      "ratioPower": 0.9,
      "flare": 0.25,
      "baseSplits": 0,
      "baseSize": [
        0.34,
        0.18,
        0.08,
        0.02
      ],
      "length": [
        1,
        0.46,
        0.34,
        0
      ],
      "lengthV": [
        0,
        0.06,
        0.05,
        0
      ],
      "taper": [
        0.58,
        0.48,
        0.35,
        1
      ],
      "curveRes": [
        10,
        8,
        5,
        1
      ],
      "curve": [
        3,
        26,
        20,
        0
      ],
      "curveBack": [
        0,
        -18,
        -12,
        0
      ],
      "curveV": [
        12,
        25,
        30,
        0
      ],
      "bendV": [
        6,
        15,
        18,
        0
      ],
      "segSplits": [
        0,
        0.1,
        0.05,
        0
      ],
      "splitAngle": [
        0,
        90,
        70,
        0
      ],
      "splitAngleV": [
        0,
        7,
        8,
        0
      ],
      "downAngle": [
        0,
        82,
        72,
        45
      ],
      "downAngleV": [
        0,
        -8,
        -12,
        10
      ],
      "rotate": [
        0,
        137,
        137,
        137
      ],
      "rotateV": [
        0,
        10,
        10,
        0
      ],
      "branches": [
        1,
        3,
        2,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.92,
        0.8,
        1
      ],
      "attractionUp": 1.25,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 0,
      "leafScale": 0.17,
      "leafScaleX": 1,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 24,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "collarRings": 2,
      "collarFillet": 0.9,
      "rootLobeAmplitude": 0.2,
      "rootLobeHeight": 0.06
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.16,
      "lengthV": 0.06,
      "radius": 0.32,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.2,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#466b3a",
      "barkDark": "#2d4825",
      "leaf": "#466b3a",
      "leafAlt": "#5c824c",
      "leafStyle": "succulent"
    }
  },
  {
    "name": "Golden Barrel Cactus",
    "botany": {
      "shape": 1,
      "scale": 1.45,
      "scaleV": 0.12,
      "levels": 1,
      "ratio": 0.24,
      "ratioPower": 1,
      "flare": 0.18,
      "baseSplits": 0,
      "baseSize": [
        0.04,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0,
        0,
        0
      ],
      "lengthV": [
        0.02,
        0,
        0,
        0
      ],
      "taper": [
        1.9,
        1,
        1,
        1
      ],
      "curveRes": [
        12,
        1,
        1,
        1
      ],
      "curve": [
        0,
        0,
        0,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        0,
        0,
        0,
        0
      ],
      "bendV": [
        0,
        0,
        0,
        0
      ],
      "segSplits": [
        0,
        0,
        0,
        0
      ],
      "splitAngle": [
        0,
        0,
        0,
        0
      ],
      "splitAngleV": [
        0,
        0,
        0,
        0
      ],
      "downAngle": [
        0,
        0,
        0,
        0
      ],
      "downAngleV": [
        0,
        0,
        0,
        0
      ],
      "rotate": [
        0,
        0,
        0,
        0
      ],
      "rotateV": [
        0,
        0,
        0,
        0
      ],
      "branches": [
        0,
        0,
        0,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 0,
      "leafScale": 0.17,
      "leafScaleX": 1,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 32,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "ringsPerSegment": [
        5,
        1,
        1,
        1
      ],
      "collarRings": 0,
      "rootLobeAmplitude": 0.1,
      "rootLobeHeight": 0.04
    },
    "roots": {
      "enabled": true,
      "count": 4,
      "length": 0.12,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.2,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#4d733c",
      "barkDark": "#304c24",
      "leaf": "#d4b84a",
      "leafAlt": "#e3cc6b",
      "leafStyle": "succulent"
    }
  },
  {
    "name": "Organ Pipe Cactus",
    "botany": {
      "shape": 3,
      "scale": 6.2,
      "scaleV": 0.9,
      "levels": 2,
      "ratio": 0.052,
      "ratioPower": 0.95,
      "flare": 0.2,
      "baseSplits": 5,
      "baseSize": [
        0.16,
        0.04,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.5,
        0,
        0
      ],
      "lengthV": [
        0.05,
        0.06,
        0,
        0
      ],
      "taper": [
        0.45,
        0.35,
        1,
        1
      ],
      "curveRes": [
        8,
        6,
        1,
        1
      ],
      "curve": [
        4,
        12,
        0,
        0
      ],
      "curveBack": [
        0,
        -10,
        0,
        0
      ],
      "curveV": [
        15,
        24,
        0,
        0
      ],
      "bendV": [
        5,
        12,
        0,
        0
      ],
      "segSplits": [
        0,
        0.08,
        0,
        0
      ],
      "splitAngle": [
        0,
        24,
        0,
        0
      ],
      "splitAngleV": [
        0,
        8,
        0,
        0
      ],
      "downAngle": [
        0,
        84,
        45,
        45
      ],
      "downAngleV": [
        0,
        -5,
        0,
        0
      ],
      "rotate": [
        0,
        137,
        137,
        137
      ],
      "rotateV": [
        0,
        12,
        0,
        0
      ],
      "branches": [
        1,
        2,
        0,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.88,
        1,
        1
      ],
      "attractionUp": 1.1,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 0,
      "leafScale": 0.17,
      "leafScaleX": 1,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 20,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "collarRings": 2,
      "rootLobeAmplitude": 0.12,
      "rootLobeHeight": 0.05
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.14,
      "lengthV": 0.06,
      "radius": 0.28,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 1,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.25,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#486b42",
      "barkDark": "#2c4628",
      "leaf": "#486b42",
      "leafAlt": "#608558",
      "leafStyle": "succulent"
    }
  },
  {
    "name": "Agave Americana",
    "botany": {
      "shape": 2,
      "scale": 1.25,
      "scaleV": 0.12,
      "levels": 2,
      "ratio": 0.04,
      "ratioPower": 1,
      "flare": 0.1,
      "baseSplits": 0,
      "baseSize": [
        0.08,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        0.22,
        0.28,
        0,
        0
      ],
      "lengthV": [
        0.05,
        0.08,
        0,
        0
      ],
      "taper": [
        0.7,
        0.55,
        1,
        1
      ],
      "curveRes": [
        5,
        4,
        1,
        1
      ],
      "curve": [
        10,
        25,
        0,
        0
      ],
      "curveBack": [
        0,
        -12,
        0,
        0
      ],
      "curveV": [
        10,
        18,
        0,
        0
      ],
      "bendV": [
        6,
        10,
        0,
        0
      ],
      "segSplits": [
        0,
        0.2,
        0,
        0
      ],
      "splitAngle": [
        0,
        24,
        0,
        0
      ],
      "splitAngleV": [
        0,
        8,
        0,
        0
      ],
      "downAngle": [
        0,
        80,
        60,
        45
      ],
      "downAngleV": [
        0,
        -5,
        0,
        0
      ],
      "rotate": [
        0,
        137.5,
        137.5,
        137.5
      ],
      "rotateV": [
        0,
        18,
        0,
        0
      ],
      "branches": [
        1,
        9,
        0,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.7,
        1,
        1
      ],
      "attractionUp": 0.4,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 72,
      "leafScale": 0.72,
      "leafScaleX": 0.13,
      "leafTuft": 0.92
    },
    "surface": {
      "trunkRadialSegments": 16,
      "minRadialSegments": 6,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "collarRings": 1,
      "rootLobeAmplitude": 0.12,
      "rootLobeHeight": 0.03
    },
    "roots": {
      "enabled": false,
      "count": 0,
      "length": 0.05,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 0,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#5d7c6e",
      "barkDark": "#3d564b",
      "leaf": "#6c8f80",
      "leafAlt": "#82a696",
      "leafStyle": "succulent"
    }
  },
  {
    "name": "Aloe Vera",
    "botany": {
      "shape": 2,
      "scale": 0.62,
      "scaleV": 0.08,
      "levels": 2,
      "ratio": 0.045,
      "ratioPower": 1,
      "flare": 0.08,
      "baseSplits": 0,
      "baseSize": [
        0.1,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        0.18,
        0.2,
        0,
        0
      ],
      "lengthV": [
        0.04,
        0.06,
        0,
        0
      ],
      "taper": [
        0.6,
        0.45,
        1,
        1
      ],
      "curveRes": [
        5,
        3,
        1,
        1
      ],
      "curve": [
        8,
        30,
        0,
        0
      ],
      "curveBack": [
        0,
        -18,
        0,
        0
      ],
      "curveV": [
        12,
        20,
        0,
        0
      ],
      "bendV": [
        8,
        12,
        0,
        0
      ],
      "segSplits": [
        0,
        0.1,
        0,
        0
      ],
      "splitAngle": [
        0,
        20,
        0,
        0
      ],
      "splitAngleV": [
        0,
        8,
        0,
        0
      ],
      "downAngle": [
        0,
        78,
        55,
        45
      ],
      "downAngleV": [
        0,
        0,
        0,
        0
      ],
      "rotate": [
        0,
        137.5,
        137.5,
        137.5
      ],
      "rotateV": [
        0,
        20,
        0,
        0
      ],
      "branches": [
        1,
        8,
        0,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.72,
        1,
        1
      ],
      "attractionUp": 0.15,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 44,
      "leafScale": 0.34,
      "leafScaleX": 0.34,
      "leafTuft": 0.95
    },
    "surface": {
      "trunkRadialSegments": 14,
      "minRadialSegments": 6,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "collarRings": 1,
      "rootLobeAmplitude": 0.08,
      "rootLobeHeight": 0.02
    },
    "roots": {
      "enabled": false,
      "count": 0,
      "length": 0.05,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 0,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#527848",
      "barkDark": "#34512e",
      "leaf": "#618a55",
      "leafAlt": "#79a16b",
      "leafStyle": "succulent"
    }
  },
  {
    "name": "Ocotillo",
    "botany": {
      "shape": 4,
      "scale": 4.2,
      "scaleV": 0.7,
      "levels": 3,
      "ratio": 0.014,
      "ratioPower": 1.05,
      "flare": 0.18,
      "baseSplits": 7,
      "baseSize": [
        0.04,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.52,
        0.2,
        0
      ],
      "lengthV": [
        0.06,
        0.08,
        0.05,
        0
      ],
      "taper": [
        0.45,
        0.65,
        0.5,
        1
      ],
      "curveRes": [
        7,
        5,
        3,
        1
      ],
      "curve": [
        16,
        28,
        12,
        0
      ],
      "curveBack": [
        -24,
        -30,
        -12,
        0
      ],
      "curveV": [
        40,
        50,
        45,
        0
      ],
      "bendV": [
        14,
        24,
        22,
        0
      ],
      "segSplits": [
        0.1,
        0.16,
        0.05,
        0
      ],
      "splitAngle": [
        20,
        36,
        35,
        0
      ],
      "splitAngleV": [
        8,
        10,
        8,
        0
      ],
      "downAngle": [
        0,
        70,
        58,
        45
      ],
      "downAngleV": [
        0,
        -10,
        8,
        0
      ],
      "rotate": [
        0,
        137,
        137,
        137
      ],
      "rotateV": [
        0,
        30,
        20,
        0
      ],
      "branches": [
        1,
        10,
        9,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.65,
        0.5,
        1
      ],
      "attractionUp": 0.9,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 50,
      "leafScale": 0.11,
      "leafScaleX": 0.22,
      "leafTuft": 0.36
    },
    "surface": {
      "trunkRadialSegments": 16,
      "minRadialSegments": 6,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "collarRings": 1,
      "rootLobeAmplitude": 0.16,
      "rootLobeHeight": 0.04
    },
    "roots": {
      "enabled": false,
      "count": 0,
      "length": 0.05,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 0,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#665c4f",
      "barkDark": "#3c352d",
      "leaf": "#4e7334",
      "leafAlt": "#c94328",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Dasylirion Sotol",
    "botany": {
      "shape": 2,
      "scale": 1.05,
      "scaleV": 0.1,
      "levels": 2,
      "ratio": 0.05,
      "ratioPower": 1,
      "flare": 0.15,
      "baseSplits": 0,
      "baseSize": [
        0.08,
        0.02,
        0.02,
        0.02
      ],
      "length": [
        0.2,
        0.25,
        0,
        0
      ],
      "lengthV": [
        0.05,
        0.08,
        0,
        0
      ],
      "taper": [
        0.72,
        0.5,
        1,
        1
      ],
      "curveRes": [
        5,
        4,
        1,
        1
      ],
      "curve": [
        18,
        38,
        0,
        0
      ],
      "curveBack": [
        0,
        -20,
        0,
        0
      ],
      "curveV": [
        10,
        20,
        0,
        0
      ],
      "bendV": [
        8,
        16,
        0,
        0
      ],
      "segSplits": [
        0,
        0.12,
        0,
        0
      ],
      "splitAngle": [
        0,
        22,
        0,
        0
      ],
      "splitAngleV": [
        0,
        8,
        0,
        0
      ],
      "downAngle": [
        0,
        75,
        54,
        45
      ],
      "downAngleV": [
        0,
        -4,
        0,
        0
      ],
      "rotate": [
        0,
        137.5,
        137.5,
        137.5
      ],
      "rotateV": [
        0,
        22,
        0,
        0
      ],
      "branches": [
        1,
        10,
        0,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        0.65,
        1,
        1
      ],
      "attractionUp": 0.55,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 72,
      "leafScale": 0.54,
      "leafScaleX": 0.08,
      "leafTuft": 0.96
    },
    "surface": {
      "trunkRadialSegments": 16,
      "minRadialSegments": 6,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "collarRings": 1,
      "rootLobeAmplitude": 0.1,
      "rootLobeHeight": 0.03
    },
    "roots": {
      "enabled": false,
      "count": 0,
      "length": 0.05,
      "lengthV": 0.06,
      "radius": 0.3,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 0,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#665c4b",
      "barkDark": "#3e372c",
      "leaf": "#6d8259",
      "leafAlt": "#899e72",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Bristlecone Pine",
    "botany": {
      "shape": 7,
      "scale": 7,
      "scaleV": 2,
      "levels": 3,
      "ratio": 0.075,
      "ratioPower": 1,
      "flare": 1,
      "baseSplits": -2,
      "baseSize": [
        0.15,
        0.1,
        0.02,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.4,
        0
      ],
      "lengthV": [
        0,
        0.25,
        0.15,
        0
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        8,
        5,
        1
      ],
      "curve": [
        20,
        30,
        10,
        0
      ],
      "curveBack": [
        -40,
        -50,
        0,
        0
      ],
      "curveV": [
        200,
        260,
        180,
        0
      ],
      "bendV": [
        80,
        160,
        90,
        0
      ],
      "segSplits": [
        0.5,
        0.45,
        0.2,
        0
      ],
      "splitAngle": [
        55,
        55,
        45,
        0
      ],
      "splitAngleV": [
        15,
        15,
        10,
        0
      ],
      "downAngle": [
        0,
        55,
        50,
        40
      ],
      "downAngleV": [
        0,
        -15,
        20,
        35
      ],
      "rotate": [
        0,
        110,
        140,
        137
      ],
      "rotateV": [
        0,
        50,
        50,
        30
      ],
      "branches": [
        1,
        7,
        16,
        0
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.7,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 220,
      "leafScale": 0.07,
      "leafScaleX": 0.35,
      "leafTuft": 0.4
    },
    "surface": {
      "trunkRadialSegments": 28,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7,
      "rootLobeAmplitude": 0.5,
      "rootLobeHeight": 0.1
    },
    "roots": {
      "enabled": true,
      "count": 6,
      "length": 0.3,
      "lengthV": 0.06,
      "radius": 0.5,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.7,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 6,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#80644c",
      "barkDark": "#4c392a",
      "leaf": "#2f5935",
      "leafAlt": "#447048",
      "leafStyle": "conifer"
    }
  },
  {
    "name": "Rowan",
    "botany": {
      "shape": 1,
      "scale": 9,
      "scaleV": 2,
      "levels": 4,
      "ratio": 0.02,
      "ratioPower": 1.3,
      "flare": 0.5,
      "baseSplits": -2,
      "baseSize": [
        0.2,
        0.1,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.55,
        0.45,
        0.3
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        8,
        6,
        3
      ],
      "curve": [
        0,
        -10,
        0,
        0
      ],
      "curveBack": [
        0,
        0,
        0,
        0
      ],
      "curveV": [
        50,
        80,
        100,
        80
      ],
      "bendV": [
        20,
        50,
        40,
        0
      ],
      "segSplits": [
        0.3,
        0.3,
        0.2,
        0
      ],
      "splitAngle": [
        35,
        35,
        35,
        0
      ],
      "splitAngleV": [
        8,
        8,
        8,
        0
      ],
      "downAngle": [
        0,
        42,
        45,
        45
      ],
      "downAngleV": [
        0,
        -20,
        15,
        15
      ],
      "rotate": [
        0,
        137,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        10,
        16,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.8,
      "flatten": 0,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 18,
      "leafScale": 0.14,
      "leafScaleX": 0.5,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.28,
      "lengthV": 0.06,
      "radius": 0.4,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.6,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.6,
      "wander": 0.5,
      "climb": 6,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#615952",
      "barkDark": "#39342f",
      "leaf": "#4c7830",
      "leafAlt": "#c24a24",
      "leafStyle": "broadleaf"
    }
  },
  {
    "name": "Japanese Maple",
    "botany": {
      "shape": 2,
      "scale": 6,
      "scaleV": 1,
      "levels": 4,
      "ratio": 0.03,
      "ratioPower": 1.15,
      "flare": 0.7,
      "baseSplits": 2,
      "baseSize": [
        0.2,
        0.06,
        0.03,
        0.02
      ],
      "length": [
        1,
        0.6,
        0.45,
        0.35
      ],
      "lengthV": [
        0,
        0.08,
        0.08,
        0.05
      ],
      "taper": [
        1,
        1,
        1,
        1
      ],
      "curveRes": [
        8,
        8,
        6,
        3
      ],
      "curve": [
        0,
        15,
        10,
        0
      ],
      "curveBack": [
        0,
        -25,
        0,
        0
      ],
      "curveV": [
        60,
        90,
        120,
        100
      ],
      "bendV": [
        20,
        60,
        60,
        0
      ],
      "segSplits": [
        0.2,
        0.4,
        0.2,
        0
      ],
      "splitAngle": [
        55,
        45,
        40,
        0
      ],
      "splitAngleV": [
        8,
        10,
        10,
        0
      ],
      "downAngle": [
        0,
        52,
        60,
        50
      ],
      "downAngleV": [
        0,
        -15,
        20,
        20
      ],
      "rotate": [
        0,
        130,
        140,
        140
      ],
      "rotateV": [
        0,
        30,
        30,
        20
      ],
      "branches": [
        1,
        8,
        14,
        12
      ],
      "branchDist": [
        0,
        0,
        0,
        0
      ],
      "radiusMod": [
        1,
        1,
        1,
        1
      ],
      "attractionUp": 0.3,
      "flatten": 0.35,
      "nodeSwell": 0,
      "nodeSpacing": 0.05,
      "leaves": 24,
      "leafScale": 0.09,
      "leafScaleX": 0.9,
      "leafTuft": 1
    },
    "surface": {
      "trunkRadialSegments": 12,
      "minRadialSegments": 4,
      "collarScale": 1.35,
      "forkRadiusConservation": 0.7
    },
    "roots": {
      "enabled": true,
      "count": 5,
      "length": 0.26,
      "lengthV": 0.06,
      "radius": 0.42,
      "radiusV": 0.12,
      "taper": 0.85,
      "emergeHeight": 0.6,
      "descent": 24,
      "exposure": 0.4,
      "laterals": 2,
      "lateralRadius": 0.5,
      "lateralLength": 0.5,
      "forks": 0.5,
      "wander": 0.5,
      "climb": 3,
      "attraction": 0.6,
      "grip": 0.5,
      "dive": 0.75
    },
    "visual": {
      "bark": "#5b4c46",
      "barkDark": "#342925",
      "leaf": "#9e2620",
      "leafAlt": "#c93c28",
      "leafStyle": "broadleaf"
    }
  }
];

export function FindSpeciesPreset(Name) {
  return SpeciesPresets.find((Entry) => Entry.name === Name) || SpeciesPresets[0];
}
