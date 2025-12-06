interface ISystemStatisticsDashEntry {
    hashrate : number;
    chipTemperature : number;
    power : number;
    timestamp : number;
}

export interface ISystemStatisticsDash {
    currentTimestamp : number;
    statistics : number[][];
}