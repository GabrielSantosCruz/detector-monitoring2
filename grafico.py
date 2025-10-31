#!/usr/bin/env python3

import re
import os
import sys
import matplotlib.pyplot as plt
import pandas as pd
from datetime import datetime
import numpy as np
import glob 

def extrair_todas_contagens_horarias(caminho_arquivo):
    """
    Procura a linha de log do vetor completo de 24 horas (gravada à 00:00:00) 
    e extrai todos os 24 valores de contagem.
    Retorna uma lista de 24 contagens.
    """
    
    regex_padrao_00h = re.compile(r'00:00:00\s+>\s+\[\d+\]\s+=>\s+\{\s*([\d,]+)\s*\}')
    
    try:
        with open(caminho_arquivo, 'r') as f:
            for linha in f:
                match_geiger = regex_padrao_00h.search(linha)
                
                if match_geiger:
                    dados_str = match_geiger.group(1)
                    contagens = [int(c.strip()) for c in dados_str.split(',')]
                    
                    if len(contagens) == 24:
                        return contagens
                    else:
                        print(f"AVISO: Arquivo {os.path.basename(caminho_arquivo)} tem um vetor incompleto (tamanho: {len(contagens)}).")
                        return []
        
        print(f"AVISO: Log de 00:00:00 (com vetor completo) não encontrado no arquivo {caminho_arquivo}.")
        return []
        
    except FileNotFoundError:
        print(f"ERRO: O arquivo {caminho_arquivo} não foi encontrado.")
        return []
    except Exception as e:
        print(f"ERRO ao processar o arquivo: {e}")
        return []

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 geiger_media_pasta.py <caminho_da_pasta_de_logs>")
        print("Exemplo: python3 geiger_media_pasta.py ./logs_diarios/")
        sys.exit(1)

    caminho_pasta = sys.argv[1]

    if not os.path.isdir(caminho_pasta):
        print(f"ERRO: O caminho fornecido '{caminho_pasta}' não é um diretório válido.")
        sys.exit(1)

    padrao_arquivos = os.path.join(caminho_pasta, 'dados_*.csv')
    caminhos_arquivos = glob.glob(padrao_arquivos)

    if not caminhos_arquivos:
        print(f"Nenhum arquivo 'dados_*.csv' encontrado no diretório: {caminho_pasta}")
        sys.exit(0)

    print(f"Encontrados {len(caminhos_arquivos)} arquivos. Processando...")
    
    dados_semanais = []
    datas_encontradas = []

    for caminho in caminhos_arquivos:
        contagens_horarias = extrair_todas_contagens_horarias(caminho)
        
        if contagens_horarias:
            dados_semanais.append(contagens_horarias)
            
            match_data = re.search(r'dados_(\d{4})(\d{2})(\d{2})\.csv', caminho)
            if match_data:
                try:
                    data_do_nome_arquivo = datetime(int(match_data.group(1)), int(match_data.group(2)), int(match_data.group(3)))
                    
                    data_corrigida = data_do_nome_arquivo - pd.Timedelta(days=1)
                    
                    datas_encontradas.append(data_corrigida)
                    
                except ValueError:
                    pass 

    if not dados_semanais:
        print("\nNenhum dado de contagem válido foi encontrado para gerar o gráfico.")
    else:
        num_dias = len(dados_semanais)
        
        df_semana = pd.DataFrame(dados_semanais, columns=range(24))
        media_por_hora = df_semana.mean(axis=0)
        
        desvio_padrao_por_hora = None
        if num_dias > 1:
            desvio_padrao_por_hora = df_semana.std(axis=0)

        media_geral_periodo = media_por_hora.mean()
        
        df_plot = pd.DataFrame({
            'Hora': list(range(24)),
            'Contagem_Media': media_por_hora
        })
        
        plt.figure(figsize=(14, 7)) 

        if desvio_padrao_por_hora is not None:
            plt.bar(df_plot['Hora'], df_plot['Contagem_Media'], 
                    yerr=desvio_padrao_por_hora, capsize=5, 
                    color='#20B2AA', alpha=0.8, 
                    label='Contagem Média por Hora')
        else:
            plt.bar(df_plot['Hora'], df_plot['Contagem_Media'], 
                    color='#20B2AA', alpha=0.8, 
                    label='Contagem (Apenas 1 dia)')
        
        plt.axhline(media_geral_periodo, color='red', linestyle='-', linewidth=2, 
                    label=f'Média do Período: {media_geral_periodo:,.0f}')
        
        titulo = f'Contagem Média de Pulsos Geiger ({num_dias} dias)'
        if datas_encontradas:
            data_inicio = min(datas_encontradas).strftime('%d/%m/%Y')
            data_fim = max(datas_encontradas).strftime('%d/%m/%Y')
            if data_inicio == data_fim:
                titulo = f'Contagem de Pulsos Geiger: {data_inicio}'
            else:
                titulo = f'Contagem Média de Pulsos Geiger ({data_inicio} a {data_fim})'

        plt.title(titulo, fontsize=16)
        plt.xlabel('Hora do Dia (00:00 a 23:00)', fontsize=12)
        plt.ylabel('Média de Pulsos Geiger', fontsize=12)
        
        plt.xticks(np.arange(0, 24, 1))
        plt.ticklabel_format(style='plain', axis='y')
        plt.legend()
        plt.grid(axis='y', alpha=0.5)
        plt.tight_layout()
        plt.show()

if __name__ == "__main__":
    main()