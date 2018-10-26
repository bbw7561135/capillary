#ifndef TRANSPORT_TABLES_H
#define TRANSPORT_TABLES_H

void MakeElecResistivityTable();
double GetElecResisitivityFromTable(double rho, double T);
void MakeThermConductivityTable();
double GetThermConductivityFromTable(double rho, double T);

#endif