export interface ISystemInfoDash {
    hostname : string;
    hashRate : number;
    power: number;
    temp : number;
    temp2: number;
    vrTemp: number;
    maxPower: number;
    voltage: number;
    nominalVoltage: number;

    frequency : number;
    isUsingFallbackStratum: boolean;

    stratumURL: string;
    stratumPort: number;
    stratumUser: string;

    fallbackStratumURL: string;
    fallbackStratumPort: number;
    fallbackStratumUser: string;

    current : number;
    coreVoltage : number;
    coreVoltageActual: number;
    bestDiff: string;

    responseTime: number;
}