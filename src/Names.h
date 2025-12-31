#pragma once
#include <string>
#include <vector>

struct Names
{
    static const std::vector<std::string>& Hindu()
    {
        static const std::vector<std::string> n =
        {
            // Hindu mythology
            "ADITI", "ADITYA", "AGNI", "ANANTA", "ANIL", "ANIRUDDHA", "ARJUNA", "ARUNA", "ARUNDHATI", "BALA", "BALADEVA", "BHARATA", "BHASKARA", "BRAHMA", "BRIJESHA", "CHANDRA",
            "DAMAYANTI", "DAMODARA", "DEVARAJA", "DEVI", "DILIPA", "DIPAKA", "DRAUPADI", "DRUPADA", "DURGA", "GANESHA", "GAURI", "GIRISHA", "GOPALA", "GOPINATHA", "GOTAMA",
            "GOVINDA", "HARI", "HARISHA", "INDIRA", "INDRA", "INDRAJIT", "INDRANI", "JAGANNATHA", "JAYA", "JAYANTI", "KALI", "KALYANI", "KAMA", "KAMALA", "KANTI", "KAPILA",
            "KARNA", "KRISHNA", "KUMARA", "KUMARI", "LAKSHMANA", "LAKSHMI", "LALITA", "MADHAVA", "MADHAVI", "MAHESHA", "MANI", "MANU", "MAYA", "MINA", "MOHANA", "MOHINI",
            "MUKESHA", "MURALI", "NALA", "NANDA", "NARAYANA", "PADMA", "PADMAVATI", "PANKAJA", "PARTHA", "PARVATI", "PITAMBARA", "PRABHU", "PRAMODA", "PRITHA", "PRIYA",
            "PURUSHOTTAMA", "RADHA", "RAGHU", "RAJANI", "RAMA", "RAMACHANDRA", "RAMESHA", "RATI", "RAVI", "REVA", "RUKMINI", "SACHIN", "SANDHYA", "SANJAYA", "SARASWATI", "SATI",
            "SAVITR", "SAVITRI", "SHAILAJA", "SHAKTI", "SHANKARA", "SHANTA", "SHANTANU", "SHIVA", "SHIVALI", "SHRI", "SHRIPATI", "SHYAMA", "SITA", "SRI", "SUMATI", "SUNDARA",
            "SUNITA", "SURESHA", "SURYA", "SUSHILA", "TARA", "UMA", "USHA", "USHAS", "VALLI", "VASANTA", "VASU", "VIDYA", "VIJAYA", "VIKRAMA", "VISHNU", "YAMA", "YAMI"
        };
        return n;
    }

    static const std::vector<std::string>& Tolkien()
    {
        static const std::vector<std::string> n =
        {
            // Tolkien's orcs
            "AZOG", "BALCMEG", "BOLDOG", "BOLG", "GOLFIMBUL", "GORBAG", "GORGOL", "GRISHNAKH", "LAGDUF", "LUG", "LUGDUSH", "MAUHUR", "MUZGASH", "ORCOBAL", "OTHROD", "RADBUG", "SHAGRAT", "SNAGA", "UFTHAK", "UGLUK"
        };
        return n;
    }
};
