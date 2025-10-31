#!/usr/bin/env python3

import re
import os
import sys
import matplotlib.pyplot as plt
import pandas as pd
from datetime import datetime
import numpy as np

def extrair_todas_contagens_horarias(caminho_arquivo):
    """
    Procura a linha de log do vetor completo de 24 horas (23:00:00) e extrai 
    todos os 24 valores de contagem, ignorando logs de 'debug' e outros.
    Retorna uma lista de 24 contagens.
    """
    
    # Regex para capturar o conteúdo dentro das chaves {} APENAS na linha 23:00:00.
    regex_padrao_23h = re.compile(r'23:00:00\s+>\s+\[\d+\]\s+=>\s+\{\s*([\d,]+)\s*\}')
    
    try:
        with open(caminho_arquivo, 'r') as f:
            for linha in f:
                match_geiger = regex_padrao_23h.search(linha)
                
                if match_geiger:
                    dados_str = match_geiger.group(1)
                    contagens = [int(c.strip()) for c in dados_str.split(',')]
                    
                    if len(contagens) == 24:
                        return contagens
                    else:
                        print(f"AVISO: Arquivo {os.path.basename(caminho_arquivo)} tem um vetor incompleto (tamanho: {len(contagens)}).")
                        return []
        
        print(f"AVISO: Log de 23:00:00 não encontrado no arquivo {caminho_arquivo}.")
        return []
        
    except FileNotFoundError:
        print(f"ERRO: O arquivo {caminho_arquivo} não foi encontrado.")
        return []
    except Exception as e:
        print(f"ERRO ao processar o arquivo: {e}")
        return []

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 geiger_serie_temporal.py <nome_do_arquivo.csv>")
        print("Exemplo: python3 geiger_serie_temporal.py dados_20250917.csv")
        sys.exit(1)

    caminho_arquivo = sys.argv[1]
    
    print(f"Processando arquivo: {caminho_arquivo}...")
    
    contagens_horarias = extrair_todas_contagens_horarias(caminho_arquivo)

    if not contagens_horarias:
        print("\nNenhum dado de contagem válido foi encontrado para gerar o gráfico.")
    else:
        df = pd.DataFrame({
            'Hora': list(range(24)),
            'Contagem_Geiger': contagens_horarias
        })
        
        media_dia = df['Contagem_Geiger'].mean()
        desvio_padrao = df['Contagem_Geiger'].std()
        
        limite_superior = media_dia + desvio_padrao
        limite_inferior = media_dia - desvio_padrao
        
        plt.figure(figsize=(14, 7)) 

        plt.bar(df['Hora'], df['Contagem_Geiger'], color='#20B2AA', alpha=0.8, label='Contagem de Pulsos')
        
        plt.axhline(media_dia, color='red', linestyle='-', linewidth=2, label=f'Média do Dia: {media_dia:,.0f}')
        
        plt.axhline(limite_superior, color='orange', linestyle='--', linewidth=1.5, label=f'+1 Desvio Padrão')
        plt.axhline(limite_inferior, color='orange', linestyle='--', linewidth=1.5, label=f'-1 Desvio Padrão')
        
        outliers = df[(df['Contagem_Geiger'] > limite_superior) | (df['Contagem_Geiger'] < limite_inferior)]
        plt.scatter(outliers['Hora'], outliers['Contagem_Geiger'], color='red', s=50, zorder=5, label='Fora do Limite')

        match_data = re.search(r'dados_(\d{4})(\d{2})(\d{2})\.csv', caminho_arquivo)
        titulo = f'Contagem de Pulsos Geiger: Série Temporal Diária'
        if match_data:
             data_formatada = datetime(int(match_data.group(1)), int(match_data.group(2)), int(match_data.group(3))).strftime('%d/%m/%Y')
             titulo = f'Contagem de Pulsos Geiger: Série Temporal em {data_formatada}'

        plt.title(titulo, fontsize=16)
        plt.xlabel('Hora do Dia (00:00 a 23:00)', fontsize=12)
        plt.ylabel('Contagem de Pulsos Geiger', fontsize=12)
        
        plt.xticks(np.arange(0, 24, 1))
        plt.ticklabel_format(style='plain', axis='y')
        plt.legend()
        plt.grid(axis='y', alpha=0.5)
        plt.tight_layout()
        plt.show()

if __name__ == "__main__":
    main()
