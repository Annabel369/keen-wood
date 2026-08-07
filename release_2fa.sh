#!/bin/bash
set -e

# Definição de Cores
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m' # Sem cor (No Color)

echo -e "${CYAN}--- Iniciando processo de Release ---${NC}"

# 1. Pega a última tag do Git
LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "")

if [ -z "$LAST_TAG" ]; then
    NEW_TAG="v6.3.0"
else
    # Lógica para aumentar o último número (ex: v6.3.0 -> v6.3.1)
    BASE=$(echo $LAST_TAG | cut -d. -f1,2)
    PATCH=$(echo $LAST_TAG | cut -d. -f3)
    if [ -z "$PATCH" ]; then PATCH=0; fi
    NEW_TAG="$BASE.$((PATCH + 1))"
fi

echo -e "Versão atual: ${YELLOW}$LAST_TAG${NC}"
echo -e "Lançando versão automática: ${GREEN}$NEW_TAG${NC}"

# 2. Processo de Git
echo -e "${CYAN}Adicionando arquivos e criando commit...${NC}"
git add .
git commit -m "Auto-release $NEW_TAG"

echo -e "${CYAN}Criando tag...${NC}"
git tag -a "$NEW_TAG" -m "Versão $NEW_TAG"

# 3. Push
echo -e "${YELLOW}Enviando para o GitHub (toque na YubiKey se necessário)...${NC}"
git push origin main
git push origin "$NEW_TAG"

echo -e "${GREEN}--- Pronto! $NEW_TAG está no ar ---${NC}"
