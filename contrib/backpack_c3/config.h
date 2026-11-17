#ifndef CONFIG_H
#define CONFIG_H

// Configuration de la variante "Timer Backpack seul" (ESP32-C3, voir
// backpack_c3.ino).

// LED de statut et bouton de binding — broches habituelles des petites
// cartes C3 : LED sur GPIO8, bouton BOOT sur GPIO9. À adapter si ta carte
// est câblée autrement (GPIO18/19 sont l'USB, GPIO20/21 l'UART0).
#define PIN_BACKPACK_LED     8
#define PIN_BACKPACK_BUTTON  9
#define BACKPACK_BAUD_RATE   460800   // débit imposé par le protocole ExpressLRS Backpack

// Bind phrase utilisée si aucune n'a encore été définie via
// BACKPACK:PHRASE: (ou après un effacement complet de la flash).
// Mets ta vraie phrase via BACKPACK:PHRASE: (ou le panneau web) plutôt
// qu'ici : elle reste alors uniquement sur l'ESP (EEPROM), jamais dans ce
// fichier ni sur GitHub. Ne mets pas ta vraie bind phrase dans ce define,
// ce fichier est public.
#define DEFAULT_BIND_PHRASE  "CHANGE-ME"

#endif
