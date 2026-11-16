# SmartAI strength per character

Scan of 2026-10-24 (`rl/scan_smartai.py`). Each character fights its own mirror match on Gotham: SmartAI as
player 2 against uniformly random controls as player 1, 50 matches each, one round per match, 3-tick decisions.
A match still running after 2500 decisions (about 125 s) is decided on remaining life, like a time over.
Raw numbers: `rl/smartai_scan.csv`.

**SmartAI win rate** is how often SmartAI beat random inputs. Low means SmartAI plays that character badly, so
a policy can beat it without learning much. **Life margin** is SmartAI's life minus random's at the end (−1..1).

## Summary

| SmartAI win rate | Characters |
|---|---:|
| under 25% (SmartAI is weaker than mashing) | 32 |
| 25–50% | 13 |
| 50–75% | 20 |
| 75% or more (good training opponents) | 85 |
| failed to run | 1 |

## Problems

- **beyond** didn't run: `[Errno 22] Invalid argument` (the process stopped; worth a look).
- **shumag**: 49 of 50 matches never ended within 2500 decisions (draws 49). The character probably stalls; its result is unreliable.
- **lukecage**: 20 of 50 matches never ended within 2500 decisions (draws 0). The character probably stalls; its result is unreliable.
- **SolomonGrundy**: 46 of 50 matches never ended within 2500 decisions (draws 2). The character probably stalls; its result is unreliable.
- **doomsdayCG**: 19 of 50 matches never ended within 2500 decisions (draws 14). The character probably stalls; its result is unreliable.

## Every character, weakest SmartAI first

| # | Character | SmartAI win rate | Random wins | Draws | Life margin | Avg decisions | Unfinished |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | storm | 0% | 50 | 0 | -0.87 | 1485 | 0 |
| 2 | omegared | 0% | 50 | 0 | -0.81 | 481 | 0 |
| 3 | robin | 0% | 50 | 0 | -0.81 | 855 | 0 |
| 4 | BlackManta_G | 0% | 50 | 0 | -0.80 | 757 | 0 |
| 5 | silversamurai | 0% | 50 | 0 | -0.75 | 1731 | 0 |
| 6 | rhino | 0% | 50 | 0 | -0.73 | 1144 | 0 |
| 7 | MvC2_War_Machine | 0% | 50 | 0 | -0.68 | 1548 | 0 |
| 8 | psylocke | 0% | 50 | 0 | -0.68 | 1796 | 0 |
| 9 | JeanGrey_AvX | 0% | 50 | 0 | -0.66 | 1558 | 0 |
| 10 | shumag | 0% | 1 | 49 | -0.00 | 2489 | 49 |
| 11 | deathstrokeOLD | 2% | 49 | 0 | -0.59 | 1212 | 1 |
| 12 | SPIDER-WOMAN | 2% | 49 | 0 | -0.56 | 1024 | 0 |
| 13 | Metallo Updated | 2% | 49 | 0 | -0.56 | 1921 | 0 |
| 14 | namor | 2% | 49 | 0 | -0.56 | 1075 | 0 |
| 15 | lukecage | 2% | 49 | 0 | -0.55 | 2124 | 20 |
| 16 | dormammu | 2% | 49 | 0 | -0.50 | 1352 | 0 |
| 17 | Peacemaker | 4% | 48 | 0 | -0.59 | 1691 | 0 |
| 18 | superboy | 4% | 48 | 0 | -0.48 | 1168 | 0 |
| 19 | nightcrawler | 4% | 48 | 0 | -0.48 | 1098 | 0 |
| 20 | SURTUR | 4% | 48 | 0 | -0.42 | 891 | 0 |
| 21 | antman1 | 4% | 48 | 0 | -0.40 | 1973 | 0 |
| 22 | invisiblewoman | 6% | 47 | 0 | -0.47 | 1428 | 0 |
| 23 | apocalypse | 8% | 46 | 0 | -0.43 | 1242 | 0 |
| 24 | hawkeye | 8% | 46 | 0 | -0.41 | 913 | 0 |
| 25 | rorschach | 10% | 45 | 0 | -0.34 | 1325 | 0 |
| 26 | gambit | 12% | 44 | 0 | -0.43 | 1361 | 0 |
| 27 | beast | 12% | 44 | 0 | -0.38 | 1199 | 0 |
| 28 | ROGUE | 22% | 39 | 0 | -0.07 | 2024 | 4 |
| 29 | wildcat | 22% | 39 | 0 | -0.04 | 2022 | 1 |
| 30 | havok | 24% | 38 | 0 | -0.26 | 1590 | 0 |
| 31 | wolverine | 24% | 38 | 0 | -0.24 | 1693 | 0 |
| 32 | blackwidow | 24% | 38 | 0 | -0.22 | 1893 | 0 |
| 33 | redskull | 28% | 36 | 0 | -0.03 | 1983 | 0 |
| 34 | sentinel | 32% | 34 | 0 | -0.46 | 362 | 0 |
| 35 | daredevil | 32% | 34 | 0 | -0.13 | 1929 | 0 |
| 36 | !ILU-KILLER_CROC | 34% | 33 | 0 | -0.19 | 1057 | 0 |
| 37 | Iceman | 34% | 33 | 0 | -0.09 | 1401 | 0 |
| 38 | SolomonGrundy | 34% | 31 | 2 | -0.01 | 2409 | 46 |
| 39 | msmarvel | 36% | 32 | 0 | -0.14 | 1104 | 0 |
| 40 | obsidian | 40% | 30 | 0 | -0.18 | 1650 | 0 |
| 41 | blackheart | 42% | 29 | 0 | -0.15 | 667 | 0 |
| 42 | CAPMARVEL | 42% | 29 | 0 | -0.03 | 1122 | 0 |
| 43 | whitequeen | 44% | 28 | 0 | -0.08 | 1747 | 0 |
| 44 | GALACTUS | 44% | 28 | 0 | -0.07 | 390 | 0 |
| 45 | colossus | 48% | 26 | 0 | -0.08 | 1173 | 0 |
| 46 | ironman | 50% | 25 | 0 | +0.01 | 1111 | 0 |
| 47 | beastboy | 50% | 25 | 0 | +0.09 | 1277 | 0 |
| 48 | humantorch | 52% | 24 | 0 | -0.04 | 1589 | 0 |
| 49 | sandman | 52% | 24 | 0 | +0.10 | 965 | 0 |
| 50 | wonder man | 54% | 23 | 0 | -0.04 | 1038 | 0 |
| 51 | hawkman | 58% | 21 | 0 | +0.14 | 1838 | 0 |
| 52 | bizarro | 58% | 21 | 0 | +0.43 | 877 | 0 |
| 53 | magneto | 60% | 20 | 0 | +0.03 | 1436 | 0 |
| 54 | groot | 60% | 20 | 0 | +0.16 | 1062 | 0 |
| 55 | kyle | 62% | 19 | 0 | +0.12 | 608 | 0 |
| 56 | doomsdayCG | 62% | 5 | 14 | +0.46 | 1892 | 19 |
| 57 | QUICKSILVER | 64% | 18 | 0 | +0.13 | 1654 | 0 |
| 58 | poisonivy_G | 64% | 18 | 0 | +0.24 | 561 | 0 |
| 59 | bluebeetle | 68% | 16 | 0 | +0.06 | 1980 | 1 |
| 60 | silversurfer | 68% | 16 | 0 | +0.20 | 890 | 0 |
| 61 | sabretooth | 70% | 15 | 0 | +0.18 | 1415 | 1 |
| 62 | MartianManhunter_T | 70% | 15 | 0 | +0.21 | 1849 | 0 |
| 63 | BatmanLegends | 72% | 14 | 0 | +0.14 | 1856 | 5 |
| 64 | elektra2 | 74% | 13 | 0 | +0.23 | 1600 | 1 |
| 65 | johnstewart | 74% | 13 | 0 | +0.25 | 835 | 0 |
| 66 | abomination | 76% | 12 | 0 | +0.17 | 1246 | 0 |
| 67 | gorillagrodd | 76% | 12 | 0 | +0.22 | 874 | 0 |
| 68 | atom | 76% | 12 | 0 | +0.24 | 1342 | 0 |
| 69 | despero | 76% | 12 | 0 | +0.25 | 802 | 0 |
| 70 | DR.FATE | 78% | 11 | 0 | +0.23 | 1004 | 0 |
| 71 | INCREDIBLEHULK | 78% | 11 | 0 | +0.27 | 1182 | 0 |
| 72 | juggernaut | 78% | 11 | 0 | +0.30 | 844 | 0 |
| 73 | shocker | 78% | 11 | 0 | +0.36 | 876 | 0 |
| 74 | NON | 80% | 10 | 0 | +0.29 | 668 | 0 |
| 75 | sinestro_updated | 82% | 9 | 0 | +0.23 | 1076 | 0 |
| 76 | ultron | 82% | 9 | 0 | +0.39 | 685 | 0 |
| 77 | blackpanther | 84% | 8 | 0 | +0.26 | 1100 | 0 |
| 78 | wasp | 84% | 8 | 0 | +0.29 | 960 | 0 |
| 79 | AGENT VENOM | 84% | 8 | 0 | +0.31 | 915 | 0 |
| 80 | DrOctopus_updated | 86% | 7 | 0 | +0.28 | 916 | 0 |
| 81 | wonderwoman | 86% | 7 | 0 | +0.29 | 2006 | 0 |
| 82 | ronan | 86% | 7 | 0 | +0.36 | 832 | 0 |
| 83 | BatmanNew | 86% | 7 | 0 | +0.43 | 708 | 0 |
| 84 | moonknight | 88% | 6 | 0 | +0.26 | 1001 | 0 |
| 85 | blackadam | 90% | 5 | 0 | +0.48 | 1467 | 0 |
| 86 | raven | 92% | 4 | 0 | +0.28 | 1788 | 0 |
| 87 | thor | 92% | 4 | 0 | +0.40 | 997 | 0 |
| 88 | clayface | 92% | 4 | 0 | +0.41 | 902 | 0 |
| 89 | Metamorpho updated | 92% | 4 | 0 | +0.43 | 827 | 0 |
| 90 | SHAZAM | 92% | 4 | 0 | +0.48 | 728 | 0 |
| 91 | deadpool | 92% | 4 | 0 | +0.51 | 1809 | 0 |
| 92 | steel | 92% | 4 | 0 | +0.52 | 994 | 0 |
| 93 | cyclops | 94% | 3 | 0 | +0.39 | 1532 | 0 |
| 94 | archangel | 94% | 3 | 0 | +0.40 | 1054 | 0 |
| 95 | mrfantastic | 94% | 3 | 0 | +0.42 | 757 | 0 |
| 96 | mystique | 94% | 3 | 0 | +0.44 | 1329 | 0 |
| 97 | doctorstrange | 94% | 3 | 0 | +0.48 | 1442 | 0 |
| 98 | redhood | 94% | 3 | 0 | +0.49 | 1037 | 0 |
| 99 | cyborg | 94% | 3 | 0 | +0.51 | 699 | 0 |
| 100 | catwoman | 94% | 3 | 0 | +0.51 | 918 | 0 |
| 101 | doom | 94% | 3 | 0 | +0.52 | 567 | 0 |
| 102 | Superman | 94% | 3 | 0 | +0.55 | 715 | 0 |
| 103 | Bane | 94% | 3 | 0 | +0.55 | 874 | 0 |
| 104 | SHEHULK | 94% | 3 | 0 | +0.61 | 1010 | 0 |
| 105 | blackmask | 96% | 2 | 0 | +0.40 | 1097 | 0 |
| 106 | mvci venom | 96% | 2 | 0 | +0.50 | 639 | 0 |
| 107 | punisherv2 | 96% | 2 | 0 | +0.59 | 674 | 0 |
| 108 | RadioactiveMan | 98% | 1 | 0 | +0.42 | 1180 | 0 |
| 109 | scarletwitch | 98% | 1 | 0 | +0.47 | 1178 | 0 |
| 110 | Sentry | 98% | 1 | 0 | +0.48 | 1334 | 0 |
| 111 | majorforce | 98% | 1 | 0 | +0.56 | 633 | 0 |
| 112 | ghostrider | 98% | 1 | 0 | +0.56 | 906 | 0 |
| 113 | red tornado_updated | 98% | 1 | 0 | +0.57 | 1091 | 0 |
| 114 | captainamerica | 98% | 1 | 0 | +0.57 | 1078 | 0 |
| 115 | Etrigan | 98% | 1 | 0 | +0.59 | 838 | 0 |
| 116 | avalanche | 98% | 1 | 0 | +0.59 | 1022 | 0 |
| 117 | HalJordan | 98% | 1 | 0 | +0.62 | 1079 | 0 |
| 118 | mvd-cheetah | 98% | 1 | 0 | +0.64 | 703 | 0 |
| 119 | General Zod | 98% | 1 | 0 | +0.65 | 662 | 0 |
| 120 | harleyquinn_new | 98% | 1 | 0 | +0.67 | 1162 | 0 |
| 121 | zatanna | 98% | 1 | 0 | +0.70 | 651 | 0 |
| 122 | mongul | 100% | 0 | 0 | +0.52 | 2113 | 0 |
| 123 | Azrael 2 | 100% | 0 | 0 | +0.59 | 892 | 0 |
| 124 | trigon | 100% | 0 | 0 | +0.60 | 791 | 0 |
| 125 | spiderman | 100% | 0 | 0 | +0.61 | 1479 | 0 |
| 126 | supergirl | 100% | 0 | 0 | +0.65 | 799 | 0 |
| 127 | Katana | 100% | 0 | 0 | +0.68 | 689 | 0 |
| 128 | luthor | 100% | 0 | 0 | +0.68 | 862 | 0 |
| 129 | ten | 100% | 0 | 0 | +0.68 | 648 | 0 |
| 130 | thing | 100% | 0 | 0 | +0.69 | 731 | 0 |
| 131 | captainatom_updated | 100% | 0 | 0 | +0.69 | 540 | 0 |
| 132 | aquaman2 | 100% | 0 | 0 | +0.70 | 642 | 0 |
| 133 | arrow | 100% | 0 | 0 | +0.70 | 933 | 0 |
| 134 | vision1 | 100% | 0 | 0 | +0.72 | 753 | 0 |
| 135 | lobo | 100% | 0 | 0 | +0.73 | 617 | 0 |
| 136 | guygardner | 100% | 0 | 0 | +0.73 | 992 | 0 |
| 137 | starfire | 100% | 0 | 0 | +0.75 | 452 | 0 |
| 138 | electro_recharged | 100% | 0 | 0 | +0.77 | 848 | 0 |
| 139 | BRAINIAC | 100% | 0 | 0 | +0.77 | 504 | 0 |
| 140 | thanos | 100% | 0 | 0 | +0.77 | 363 | 0 |
| 141 | batgirl_m | 100% | 0 | 0 | +0.79 | 501 | 0 |
| 142 | amazo | 100% | 0 | 0 | +0.81 | 624 | 0 |
| 143 | darkseid | 100% | 0 | 0 | +0.81 | 557 | 0 |
| 144 | Joker | 100% | 0 | 0 | +0.83 | 1424 | 0 |
| 145 | plastic | 100% | 0 | 0 | +0.86 | 839 | 0 |
| 146 | THEflash | 100% | 0 | 0 | +0.86 | 782 | 0 |
| 147 | firestorm update | 100% | 0 | 0 | +0.89 | 617 | 0 |
| 148 | silverbanshee | 100% | 0 | 0 | +0.93 | 1045 | 0 |
| 149 | nightwing | 100% | 0 | 0 | +0.97 | 886 | 0 |
| 150 | professor zoom | 100% | 0 | 0 | +1.00 | 453 | 0 |
