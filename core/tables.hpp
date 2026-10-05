// Fichier généré par core/gen_tables.js : ne pas éditer à la main.
#pragma once
#include <vector>
#include <string>

namespace hexa {

struct ScaleDef { const char* key; std::vector<int> steps; const char* en; const char* fr; };   // steps vide = chromatique
inline const std::vector<ScaleDef>& scaleTable() {
  static const std::vector<ScaleDef> t = {
    { "chromatique", {  }, "Chromatic", "Chromatique" },
    { "majeur", { 0, 2, 4, 5, 7, 9, 11 }, "Major", "Majeur" },
    { "mineur", { 0, 2, 3, 5, 7, 8, 10 }, "Natural minor", "Mineur naturel" },
    { "dorien", { 0, 2, 3, 5, 7, 9, 10 }, "Dorian", "Dorien" },
    { "phrygien", { 0, 1, 3, 5, 7, 8, 10 }, "Phrygian", "Phrygien" },
    { "lydien", { 0, 2, 4, 6, 7, 9, 11 }, "Lydian", "Lydien" },
    { "mixolydien", { 0, 2, 4, 5, 7, 9, 10 }, "Mixolydian", "Mixolydien" },
    { "locrien", { 0, 1, 3, 5, 6, 8, 10 }, "Locrian", "Locrien" },
    { "mineurharm", { 0, 2, 3, 5, 7, 8, 11 }, "Harmonic minor", "Mineur harmonique" },
    { "mineurmel", { 0, 2, 3, 5, 7, 9, 11 }, "Melodic minor", "Mineur mélodique" },
    { "tons", { 0, 2, 4, 6, 8, 10 }, "Whole tone", "Par tons" },
    { "blues", { 0, 3, 5, 6, 7, 10 }, "Blues", "Blues" },
    { "pentamajeur", { 0, 2, 4, 7, 9 }, "Major pentatonic", "Pentatonique majeure" },
    { "pentamineur", { 0, 3, 5, 7, 10 }, "Minor pentatonic", "Pentatonique mineure" },
    { "hongrois", { 0, 2, 3, 6, 7, 8, 11 }, "Hungarian minor", "Mineur hongrois" },
    { "phrygiendom", { 0, 1, 4, 5, 7, 8, 10 }, "Phrygian dominant", "Phrygien dominant" },
    { "doubleharm", { 0, 1, 4, 5, 7, 8, 11 }, "Double harmonic", "Double harmonique" },
    { "dim12", { 0, 1, 3, 4, 6, 7, 9, 10 }, "Diminished (half-whole)", "Diminuée (demi-ton / ton)" },
    { "dim21", { 0, 2, 3, 5, 6, 8, 9, 11 }, "Diminished (whole-half)", "Diminuée (ton / demi-ton)" },
    { "insen", { 0, 1, 5, 7, 10 }, "In sen (Japanese)", "In sen (japonaise)" },
    { "hirajoshi", { 0, 2, 3, 7, 8 }, "Hirajoshi", "Hirajoshi" },
    { "bebop", { 0, 2, 4, 5, 7, 9, 10, 11 }, "Bebop dominant", "Bebop dominante" },
    { "augmentee", { 0, 3, 4, 7, 8, 11 }, "Augmented", "Augmentée" },
  };
  return t;
}

inline const int (&kwTable())[8][8] {
  static const int t[8][8] = {
    { 1, 34, 5, 26, 11, 9, 14, 43 },
    { 25, 51, 3, 27, 24, 42, 21, 17 },
    { 6, 40, 29, 4, 7, 59, 64, 47 },
    { 33, 62, 39, 52, 15, 53, 56, 31 },
    { 12, 16, 8, 23, 2, 20, 35, 45 },
    { 44, 32, 48, 18, 46, 57, 50, 28 },
    { 13, 55, 63, 22, 36, 37, 30, 49 },
    { 10, 54, 60, 41, 19, 61, 38, 58 },
  };
  return t;
}

inline int trigramIndex(const std::string& bits3) {
  if (bits3 == "100") return 1;
  if (bits3 == "101") return 6;
  if (bits3 == "110") return 7;
  if (bits3 == "111") return 0;
  if (bits3 == "010") return 2;
  if (bits3 == "001") return 3;
  if (bits3 == "000") return 4;
  if (bits3 == "011") return 5;
  return 0;
}

struct HexName { const char* pinyin; const char* fr; const char* en; };
inline const HexName& hexName(int n) {
  static const HexName t[65] = {
    { "", "", "" },
    { "Qian", "Le Créateur", "The Creative" },
    { "Kun", "Le Réceptif", "The Receptive" },
    { "Zhun", "La Difficulté initiale", "Difficulty at the Beginning" },
    { "Meng", "La Folie juvénile", "Youthful Folly" },
    { "Xu", "L'Attente", "Waiting" },
    { "Song", "Le Conflit", "Conflict" },
    { "Shi", "L'Armée", "The Army" },
    { "Bi", "La Solidarité", "Holding Together" },
    { "Xiao Xu", "Le Pouvoir d'apprivoisement du petit", "Small Taming" },
    { "Lü", "La Marche", "Treading" },
    { "Tai", "La Paix", "Peace" },
    { "Pi", "La Stagnation", "Standstill" },
    { "Tong Ren", "Communauté avec les hommes", "Fellowship with Men" },
    { "Da You", "Possession en grand", "Possession in Great Measure" },
    { "Qian", "L'Humilité", "Modesty" },
    { "Yu", "L'Enthousiasme", "Enthusiasm" },
    { "Sui", "La Suite", "Following" },
    { "Gu", "Le Travail sur ce qui est corrompu", "Work on What Has Been Spoiled" },
    { "Lin", "L'Approche", "Approach" },
    { "Guan", "La Contemplation", "Contemplation" },
    { "Shi He", "Mordre au travers", "Biting Through" },
    { "Bi", "La Grâce", "Grace" },
    { "Bo", "L'Éclatement", "Splitting Apart" },
    { "Fu", "Le Retour", "Return" },
    { "Wu Wang", "L'Innocence", "Innocence" },
    { "Da Xu", "Le Pouvoir d'apprivoisement du grand", "Great Taming" },
    { "Yi", "Les Commissures des lèvres", "Nourishment" },
    { "Da Guo", "La Prépondérance du grand", "Preponderance of the Great" },
    { "Kan", "L'Insondable", "The Abysmal" },
    { "Li", "L'Adhérent, le Feu", "The Clinging, Fire" },
    { "Xian", "L'Influence", "Influence" },
    { "Heng", "La Durée", "Duration" },
    { "Dun", "La Retraite", "Retreat" },
    { "Da Zhuang", "La Puissance du grand", "The Power of the Great" },
    { "Jin", "Le Progrès", "Progress" },
    { "Ming Yi", "L'Obscurcissement de la lumière", "Darkening of the Light" },
    { "Jia Ren", "La Famille", "The Family" },
    { "Kui", "L'Opposition", "Opposition" },
    { "Jian", "L'Obstacle", "Obstruction" },
    { "Xie", "La Libération", "Deliverance" },
    { "Sun", "La Diminution", "Decrease" },
    { "Yi", "L'Augmentation", "Increase" },
    { "Guai", "La Percée", "Breakthrough" },
    { "Gou", "Venir à la rencontre", "Coming to Meet" },
    { "Cui", "Le Rassemblement", "Gathering Together" },
    { "Sheng", "La Poussée vers le haut", "Pushing Upward" },
    { "Kun", "L'Accablement", "Oppression" },
    { "Jing", "Le Puits", "The Well" },
    { "Ge", "La Révolution", "Revolution" },
    { "Ding", "Le Chaudron", "The Cauldron" },
    { "Zhen", "L'Ébranlement, le Tonnerre", "The Arousing, Thunder" },
    { "Gen", "L'Immobilisation, la Montagne", "Keeping Still, Mountain" },
    { "Jian", "Le Développement", "Development" },
    { "Gui Mei", "L'Épousée", "The Marrying Maiden" },
    { "Feng", "L'Abondance", "Abundance" },
    { "Lü", "Le Voyageur", "The Wanderer" },
    { "Xun", "Le Doux, le Vent", "The Gentle, Wind" },
    { "Dui", "Le Serein, le Lac", "The Joyous, Lake" },
    { "Huan", "La Dissolution", "Dispersion" },
    { "Jie", "La Limitation", "Limitation" },
    { "Zhong Fu", "La Vérité intérieure", "Inner Truth" },
    { "Xiao Guo", "La Prépondérance du petit", "Preponderance of the Small" },
    { "Ji Ji", "Après l'accomplissement", "After Completion" },
    { "Wei Ji", "Avant l'accomplissement", "Before Completion" },
  };
  return t[n < 1 || n > 64 ? 0 : n];
}

}  // namespace hexa
