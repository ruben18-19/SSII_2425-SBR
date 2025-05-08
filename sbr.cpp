#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm> // Para std::transform y std::remove
#include <cctype>    // Para std::tolower y std::isspace

// --- Definición de Estructuras de Datos ---

// Representa un hecho o una proposición.
struct Hecho {
    std::string nombre;
    double factorCerteza = 0.0; // Se establece al leer de BH o al inferir.
};

// Operadores lógicos para las condiciones de las reglas
enum class OperadorLogico {
    NINGUNO, // Condición con un solo hecho
    Y,
    O
};

// Representa el antecedente (parte "Si") de una regla
struct Antecedente {
    std::vector<Hecho> condiciones;
    OperadorLogico operador = OperadorLogico::NINGUNO;
};

// Representa una regla en la Base de Conocimiento
struct Regla {
    std::string id;
    Antecedente antecedente;
    Hecho consecuente;
    double factorCertezaRegla; // FC de la implicación de la regla
};

// Contenedor para la Base de Conocimiento
struct BaseConocimiento {
    std::vector<Regla> reglas;
};

// Contenedor para la Base de Hechos
struct BaseHechos {
    std::vector<Hecho> hechos_iniciales;
    Hecho objetivo;
    std::map<std::string, double> fc_memoria; // Memoria de trabajo (FCs conocidos o inferidos)
};

// --- Funciones Auxiliares para Parseo ---

// Convierte un string a minúsculas
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// Elimina espacios en blanco al inicio y al final de un string
std::string trim(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return ""; // String contiene solo espacios en blanco
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, (end - start + 1));
}

// Parsea la cadena del antecedente para extraer los hechos y el operador
bool parsearAntecedente(const std::string& alfaStr, Antecedente& antecedente) {
    std::string alfaLower = toLower(alfaStr);
    std::string opY = " y ";
    std::string opO = " o ";
    size_t posY = alfaLower.find(opY);
    size_t posO = alfaLower.find(opO);

    std::vector<std::string> literalesStr;

    if (posY != std::string::npos && (posO == std::string::npos || posY < posO)) { // Asumimos que no se mezclan 'y' y 'o' en el mismo nivel sin paréntesis (no especificado)
        antecedente.operador = OperadorLogico::Y;
        size_t start = 0;
        while (posY != std::string::npos) {
            literalesStr.push_back(trim(alfaStr.substr(start, posY - start)));
            start = posY + opY.length();
            posY = alfaLower.find(opY, start);
        }
        literalesStr.push_back(trim(alfaStr.substr(start)));
    } else if (posO != std::string::npos) {
        antecedente.operador = OperadorLogico::O;
        size_t start = 0;
        while (posO != std::string::npos) {
            literalesStr.push_back(trim(alfaStr.substr(start, posO - start)));
            start = posO + opO.length();
            posO = alfaLower.find(opO, start);
        }
        literalesStr.push_back(trim(alfaStr.substr(start)));
    } else {
        antecedente.operador = OperadorLogico::NINGUNO;
        literalesStr.push_back(trim(alfaStr));
    }

    for (const auto& litStr : literalesStr) {
        if (!litStr.empty()) {
            Hecho h;
            h.nombre = litStr;
            // h.factorCerteza no se establece aquí, se buscará/inferirá
            antecedente.condiciones.push_back(h);
        } else {
            std::cerr << "Error: Literal vacío encontrado en antecedente: '" << alfaStr << "'" << std::endl;
            return false;
        }
    }
    return !antecedente.condiciones.empty();
}


// --- Funciones de Carga ---

bool cargarReglas(const std::string& nombreArchivo, BaseConocimiento& bc) {
    std::ifstream archivo(nombreArchivo);
    if (!archivo.is_open()) {
        std::cerr << "Error al abrir el archivo de reglas: " << nombreArchivo << std::endl;
        return false;
    }

    std::string linea;
    int numReglasEsperadas = 0;

    // Leer número de reglas
    if (std::getline(archivo, linea)) {
        try {
            numReglasEsperadas = std::stoi(trim(linea));
        } catch (const std::invalid_argument& ia) {
            std::cerr << "Error: Número de reglas inválido: " << linea << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Archivo de reglas vacío o formato incorrecto en la primera línea." << std::endl;
        return false;
    }

    for (int i = 0; i < numReglasEsperadas; ++i) {
        if (!std::getline(archivo, linea)) {
            std::cerr << "Error: Fin de archivo inesperado. Se esperaban " << numReglasEsperadas << " reglas, se leyeron " << i << "." << std::endl;
            return false;
        }

        linea = trim(linea);
        if (linea.empty()) { // Omitir líneas vacías si las hubiera, aunque no deberían
            i--;
            continue;
        }

        Regla r;

        // 1. Extraer ID
        size_t posColon = linea.find(':');
        if (posColon == std::string::npos) {
            std::cerr << "Error de formato en regla (falta ':'): " << linea << std::endl;
            return false;
        }
        r.id = trim(linea.substr(0, posColon));
        std::string defReglaCompleta = trim(linea.substr(posColon + 1));

        // 2. Extraer Factor de Certeza de la regla
        std::string fcMarkerLower = "fc=";
        std::string defReglaLower = toLower(defReglaCompleta);
        size_t posFc = defReglaLower.rfind(fcMarkerLower); // rfind para encontrar el último por si "fc=" aparece en un hecho

        if (posFc == std::string::npos) {
            std::cerr << "Error de formato en regla (falta 'FC='): " << defReglaCompleta << std::endl;
            return false;
        }
        
        // Buscar la coma que precede a "FC="
        size_t posComaAntesFc = defReglaCompleta.rfind(',', posFc);
        if (posComaAntesFc == std::string::npos) {
            std::cerr << "Error de formato en regla (falta ',' antes de 'FC='): " << defReglaCompleta << std::endl;
            return false;
        }

        std::string fcValorStr = trim(defReglaCompleta.substr(posFc + fcMarkerLower.length()));
        try {
            r.factorCertezaRegla = std::stod(fcValorStr);
        } catch (const std::invalid_argument& ia) {
            std::cerr << "Error: Factor de certeza de regla inválido: " << fcValorStr << " en " << defReglaCompleta << std::endl;
            return false;
        }

        std::string reglaSiEntonces = trim(defReglaCompleta.substr(0, posComaAntesFc));

        // 3. Parsear "Si alfa Entonces beta"
        std::string reglaSiEntoncesLower = toLower(reglaSiEntonces);
        std::string siMarker = "si ";
        std::string entoncesMarker = " entonces ";

        size_t posSi = reglaSiEntoncesLower.find(siMarker);
        if (posSi != 0) { // "Si" debe estar al principio
            std::cerr << "Error de formato en regla (falta 'Si' o no está al inicio): " << reglaSiEntonces << std::endl;
            return false;
        }

        size_t posEntonces = reglaSiEntoncesLower.find(entoncesMarker, posSi + siMarker.length());
        if (posEntonces == std::string::npos) {
            std::cerr << "Error de formato en regla (falta 'Entonces'): " << reglaSiEntonces << std::endl;
            return false;
        }

        std::string alfaStr = trim(reglaSiEntonces.substr(posSi + siMarker.length(), posEntonces - (posSi + siMarker.length())));
        std::string betaStr = trim(reglaSiEntonces.substr(posEntonces + entoncesMarker.length()));

        if (alfaStr.empty()) {
            std::cerr << "Error: Antecedente vacío en regla: " << reglaSiEntonces << std::endl;
            return false;
        }
        if (betaStr.empty()) {
             std::cerr << "Error: Consecuente vacío en regla: " << reglaSiEntonces << std::endl;
            return false;
        }

        // Parsear antecedente
        if (!parsearAntecedente(alfaStr, r.antecedente)) {
            std::cerr << "Error al parsear antecedente para regla: " << r.id << std::endl;
            return false;
        }

        // Parsear consecuente (es un solo Hecho)
        r.consecuente.nombre = trim(betaStr);
        // r.consecuente.factorCerteza no se establece aquí

        bc.reglas.push_back(r);
    }
    if (bc.reglas.size() != numReglasEsperadas) {
        std::cerr << "Advertencia: Se esperaban " << numReglasEsperadas << " reglas, pero se cargaron " << bc.reglas.size() << "." << std::endl;
    }


    return true;
}

bool cargarHechos(const std::string& nombreArchivo, BaseHechos& bh) {
    std::ifstream archivo(nombreArchivo);
    if (!archivo.is_open()) {
        std::cerr << "Error al abrir el archivo de hechos: " << nombreArchivo << std::endl;
        return false;
    }

    std::string linea;
    int numHechosEsperados = 0;

    // Leer número de hechos
    if (std::getline(archivo, linea)) {
        try {
            numHechosEsperados = std::stoi(trim(linea));
        } catch (const std::invalid_argument& ia) {
            std::cerr << "Error: Número de hechos inválido: " << linea << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Archivo de hechos vacío o formato incorrecto en la primera línea." << std::endl;
        return false;
    }

    for (int i = 0; i < numHechosEsperados; ++i) {
        if (!std::getline(archivo, linea)) {
            std::cerr << "Error: Fin de archivo inesperado. Se esperaban " << numHechosEsperados << " hechos, se leyeron " << i << "." << std::endl;
            return false;
        }
        linea = trim(linea);
         if (linea.empty()) {
            i--;
            continue;
        }

        Hecho h;
        std::string fcMarkerLower = "fc=";
        
        // Buscar la última coma, ya que el hecho puede tener comas en su nombre (aunque no es lo ideal)
        // La estructura es "hecho, FC=numero"
        size_t posComa = linea.rfind(',');
        if (posComa == std::string::npos) {
            std::cerr << "Error de formato en hecho (falta ','): " << linea << std::endl;
            return false;
        }

        h.nombre = trim(linea.substr(0, posComa));
        std::string fcParte = trim(linea.substr(posComa + 1));
        std::string fcParteLower = toLower(fcParte);
        
        size_t posFc = fcParteLower.find(fcMarkerLower);
        if (posFc != 0) { // "FC=" debe estar al inicio de esta parte
            std::cerr << "Error de formato en hecho (falta 'FC=' o no está en la posición correcta): " << linea << std::endl;
            return false;
        }

        std::string fcValorStr = trim(fcParte.substr(posFc + fcMarkerLower.length()));
        try {
            h.factorCerteza = std::stod(fcValorStr);
        } catch (const std::invalid_argument& ia) {
            std::cerr << "Error: Factor de certeza de hecho inválido: " << fcValorStr << " en " << linea << std::endl;
            return false;
        }

        bh.hechos_iniciales.push_back(h);
        bh.fc_memoria[h.nombre] = h.factorCerteza;
    }

    if (bh.hechos_iniciales.size() != numHechosEsperados) {
        std::cerr << "Advertencia: Se esperaban " << numHechosEsperados << " hechos, pero se cargaron " << bh.hechos_iniciales.size() << "." << std::endl;
    }


    // Leer "Objetivo" y el hecho objetivo
    bool leidoKeywordObjetivo = false;
    while (std::getline(archivo, linea)) {
        linea = trim(linea);
        if (linea.empty()) continue;

        if (!leidoKeywordObjetivo) {
            if (toLower(linea) == "objetivo") {
                leidoKeywordObjetivo = true;
            } else {
                std::cerr << "Error: Se esperaba la palabra clave 'Objetivo', se encontró: " << linea << std::endl;
                return false;
            }
        } else {
            bh.objetivo.nombre = trim(linea);
            // bh.objetivo.factorCerteza se calculará
            if (bh.objetivo.nombre.empty()) {
                std::cerr << "Error: Hecho objetivo no especificado o vacío." << std::endl;
                return false;
            }
            return true; // Objetivo leído correctamente
        }
    }

    if (!leidoKeywordObjetivo) {
         std::cerr << "Error: Palabra clave 'Objetivo' no encontrada." << std::endl;
         return false;
    }
    if (bh.objetivo.nombre.empty()){
        std::cerr << "Error: Hecho objetivo no especificado después de la palabra clave 'Objetivo'." << std::endl;
        return false;
    }

    return true; // Debería haber retornado antes si todo fue bien.
}


// --- Funciones de Impresión para Verificación (Opcional) ---
void imprimirBaseConocimiento(const BaseConocimiento& bc) {
    std::cout << "--- Base de Conocimiento ---" << std::endl;
    std::cout << "Número de Reglas: " << bc.reglas.size() << std::endl;
    for (const auto& regla : bc.reglas) {
        std::cout << regla.id << ": Si ";
        for (size_t i = 0; i < regla.antecedente.condiciones.size(); ++i) {
            std::cout << regla.antecedente.condiciones[i].nombre;
            if (i < regla.antecedente.condiciones.size() - 1) {
                if (regla.antecedente.operador == OperadorLogico::Y) std::cout << " y ";
                else if (regla.antecedente.operador == OperadorLogico::O) std::cout << " o ";
            }
        }
        std::cout << " Entonces " << regla.consecuente.nombre;
        std::cout << ", FC = " << regla.factorCertezaRegla << std::endl;
    }
    std::cout << "---------------------------" << std::endl;
}

void imprimirBaseHechos(const BaseHechos& bh) {
    std::cout << "--- Base de Hechos ---" << std::endl;
    std::cout << "Número de Hechos Iniciales: " << bh.hechos_iniciales.size() << std::endl;
    for (const auto& hecho : bh.hechos_iniciales) {
        std::cout << hecho.nombre << ", FC = " << hecho.factorCerteza << std::endl;
    }
    std::cout << "Objetivo: " << bh.objetivo.nombre << std::endl;
    std::cout << "--- FC Memoria Inicial ---" << std::endl;
    for(const auto& pair : bh.fc_memoria) {
        std::cout << pair.first << ": " << pair.second << std::endl;
    }
    std::cout << "----------------------" << std::endl;
}


// --- Función Principal para Pruebas ---
int main() {
    BaseConocimiento bc;
    BaseHechos bh;

    // Crear ficheros de prueba (esto es solo para que el ejemplo sea autocontenido)
    // En un caso real, estos ficheros existirían previamente.
    std::ofstream reglasFile("Prueba-1.reglas");
    if (reglasFile.is_open()) {
        reglasFile << "4\n";
        reglasFile << "R1: Si h2 o h3 Entonces h1, FC = 0.5\n";
        reglasFile << "R2: Si h4 Entonces h1, FC = 1\n";
        reglasFile << "R3: Si h5 y h6 Entonces h3, FC = 0.7\n";
        reglasFile << "R4: Si h7 Entonces h3, FC = -0.5\n";
        reglasFile.close();
    } else {
        std::cerr << "No se pudo crear Prueba-1.reglas para el test." << std::endl;
        return 1;
    }

    std::ofstream hechosFile("Prueba-1.hechos");
    if (hechosFile.is_open()) {
        hechosFile << "5\n";
        hechosFile << "h2, FC = 0.3\n";
        hechosFile << "h4, FC = 0.6\n";
        hechosFile << "h5, FC = 0.6\n";
        hechosFile << "h6, FC = 0.9\n";
        hechosFile << "h7, FC = 0.5\n";
        hechosFile << "Objetivo\n";
        hechosFile << "h1\n";
        hechosFile.close();
    } else {
        std::cerr << "No se pudo crear Prueba-1.hechos para el test." << std::endl;
        return 1;
    }

    std::cout << "Cargando Base de Conocimiento desde Prueba-1.reglas..." << std::endl;
    if (cargarReglas("Prueba-1.reglas", bc)) {
        std::cout << "Base de Conocimiento cargada exitosamente." << std::endl;
        imprimirBaseConocimiento(bc);
    } else {
        std::cout << "Fallo al cargar la Base de Conocimiento." << std::endl;
        return 1;
    }

    std::cout << "\nCargando Base de Hechos desde Prueba-1.hechos..." << std::endl;
    if (cargarHechos("Prueba-1.hechos", bh)) {
        std::cout << "Base de Hechos cargada exitosamente." << std::endl;
        imprimirBaseHechos(bh);
    } else {
        std::cout << "Fallo al cargar la Base de Hechos." << std::endl;
        return 1;
    }

    // Aquí iría la llamada al motor de inferencia en el futuro
    // motorDeInferencia(bc, bh);

    return 0;
}