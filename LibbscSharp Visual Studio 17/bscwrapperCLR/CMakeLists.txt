cmake_minimum_required(VERSION 3.15)

project(bscwrapperCLR LANGUAGES CXX)

# ✅ Forcer le mode Release par défaut
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 🛠️ Spécifier le type de bibliothèque
add_library(bscwrapperCLR SHARED bscwrapperCLR.cpp)

# ✅ Activer le support C++/CLI (/clr) avec MSVC
if(MSVC)
  set_property(TARGET bscwrapperCLR PROPERTY COMMON_LANGUAGE_RUNTIME "/clr")
  set_target_properties(bscwrapperCLR PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

# 📚 Ajouter le répertoire des headers si besoin
target_include_directories(bscwrapperCLR PRIVATE ${PROJECT_SOURCE_DIR})

# 📦 Lier la bibliothèque libbsc.lib
target_link_directories(bscwrapperCLR PRIVATE ${PROJECT_SOURCE_DIR}/libs)
target_link_libraries(bscwrapperCLR PRIVATE libbsc)

# ✨ Définir l'export en tant que DLL .NET interopérable
set_target_properties(bscwrapperCLR PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)