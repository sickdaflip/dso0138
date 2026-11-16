// SPDX-License-Identifier: MIT
#include "i18n.h"

namespace {

uint8_t current = LANG_EN_US;

const char *const EN_US[STR_COUNT] = {
	"RUN", "STOP", "WAIT", "ROLL", "TRIG'D",
	"Auto", "Normal", "Single",
	"GND", "AC", "DC",
	"Freq", "Vpp", "Vrms", "Avg", "Duty", "Clipped",
	"Settings", "Language", "Measurements", "Zero calibration", "Center trace",
	"Factory reset", "About", "Back",
	"On", "Off",
	"Set CPL to GND, then move", "SEN1 and SEN2 through all positions.", "All ranges calibrated",
	"Trace centered", "Settings reset", "Saved",
	"SEL: select  \xC2\xB7  -/+: adjust", "OK: run/stop  \xC2\xB7  hold OK: settings",
	"Timebase", "Trigger mode", "Trigger edge", "Trigger level", "Vertical position",
	"Trigger position",
	"Open source, MIT license",
	"Trace color",
	"White", "Yellow", "Cyan", "Green", "Orange", "Pink",
};

const char *const EN_GB[STR_COUNT] = {
	"RUN", "STOP", "WAIT", "ROLL", "TRIG'D",
	"Auto", "Normal", "Single",
	"GND", "AC", "DC",
	"Freq", "Vpp", "Vrms", "Avg", "Duty", "Clipped",
	"Settings", "Language", "Measurements", "Zero calibration", "Centre trace",
	"Factory reset", "About", "Back",
	"On", "Off",
	"Set CPL to GND, then move", "SEN1 and SEN2 through all positions.", "All ranges calibrated",
	"Trace centred", "Settings reset", "Saved",
	"SEL: select  \xC2\xB7  -/+: adjust", "OK: run/stop  \xC2\xB7  hold OK: settings",
	"Timebase", "Trigger mode", "Trigger edge", "Trigger level", "Vertical position",
	"Trigger position",
	"Open source, MIT licence",
	"Trace colour",
	"White", "Yellow", "Cyan", "Green", "Orange", "Pink",
};

const char *const DE[STR_COUNT] = {
	"RUN", "STOP", "WARTE", "ROLL", "TRIG",
	"Auto", "Normal", "Einzel",
	"GND", "AC", "DC",
	"Freq", "Vss", "Veff", "Mittel", "Tastgrad", "Übersteuert",
	"Einstellungen", "Sprache", "Messwerte", "Nullpunkt kalibrieren", "Spur zentrieren",
	"Werkseinstellungen", "Info", "Zurück",
	"Ein", "Aus",
	"CPL auf GND stellen, dann", "SEN1 und SEN2 durch alle Stellungen.", "Alle Bereiche kalibriert",
	"Spur zentriert", "Zurückgesetzt", "Gespeichert",
	"SEL: Auswahl  \xC2\xB7  -/+: ändern", "OK: Start/Stopp  \xC2\xB7  OK halten: Menü",
	"Zeitbasis", "Triggermodus", "Triggerflanke", "Triggerpegel", "Vertikale Position",
	"Triggerposition",
	"Open Source, MIT-Lizenz",
	"Spurfarbe",
	"Weiß", "Gelb", "Cyan", "Grün", "Orange", "Pink",
};

const char *const ES[STR_COUNT] = {
	"RUN", "STOP", "ESPERA", "ROLL", "DISP",
	"Auto", "Normal", "Único",
	"GND", "AC", "DC",
	"Frec", "Vpp", "Vrms", "Media", "Ciclo", "Saturado",
	"Ajustes", "Idioma", "Mediciones", "Calibrar cero", "Centrar traza",
	"Restablecer", "Acerca de", "Atrás",
	"Sí", "No",
	"Ponga CPL en GND y mueva", "SEN1 y SEN2 por todas las posiciones.", "Todos los rangos calibrados",
	"Traza centrada", "Ajustes restablecidos", "Guardado",
	"SEL: elegir  \xC2\xB7  -/+: ajustar", "OK: inicio/parada  \xC2\xB7  mantener OK: ajustes",
	"Base de tiempo", "Modo de disparo", "Flanco", "Nivel de disparo", "Posición vertical",
	"Posición del disparo",
	"Código abierto, licencia MIT",
	"Color de traza",
	"Blanco", "Amarillo", "Cian", "Verde", "Naranja", "Rosa",
};

const char *const FR[STR_COUNT] = {
	"RUN", "STOP", "ATTENTE", "ROLL", "DÉCL",
	"Auto", "Normal", "Unique",
	"GND", "AC", "DC",
	"Fréq", "Vcc", "Veff", "Moy", "Rapport", "Saturé",
	"Réglages", "Langue", "Mesures", "Calibrer le zéro", "Centrer la trace",
	"Réinitialiser", "À propos", "Retour",
	"Oui", "Non",
	"Mettez CPL sur GND, puis passez", "SEN1 et SEN2 sur toutes les positions.",
	"Toutes les gammes calibrées",
	"Trace centrée", "Réglages réinitialisés", "Enregistré",
	"SEL : choisir  \xC2\xB7  -/+ : régler", "OK : marche/arrêt  \xC2\xB7  maintenir OK : réglages",
	"Base de temps", "Mode de déclenchement", "Front", "Niveau de déclenchement",
	"Position verticale", "Position du déclenchement",
	"Open source, licence MIT",
	"Couleur de trace",
	"Blanc", "Jaune", "Cyan", "Vert", "Orange", "Rose",
};

const char *const *const TABLES[LANG_COUNT] = {EN_US, EN_GB, DE, ES, FR};
const char *const NAMES[LANG_COUNT] = {"English (US)", "English (UK)", "Deutsch", "Español", "Français"};

}  // namespace

namespace I18n {

void set(uint8_t lang) {
	current = lang < LANG_COUNT ? lang : uint8_t(LANG_EN_US);
}

uint8_t get() {
	return current;
}

const char *tr(Str s) {
	return TABLES[current][s];
}

const char *langName(uint8_t lang) {
	return NAMES[lang < LANG_COUNT ? lang : 0];
}

}  // namespace I18n
