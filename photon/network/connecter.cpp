// the purpose of this file is to connect DBC files to their respective CAN IDs.
// also im assuming (its not true) that the DBC file will be accessible like a dictionary
// and same for can IDs but im making it 

EXAMPLE INPUT :
canid_map<int, int> order;
    // Mapping values to keys
    order[can1id] = vector<string> info = {"ID", " Name", "DLC", "Transmitter"};;
    order[can2id] = vector<string> info = {"ID", " Name", "DLC", "Transmitter"};;
    order[can3id] = vector<string> info = {"ID", " Name", "DLC", "Transmitter"};;

Can1idSG_map<int, int> order;

    order[signalname1] = vector<string> info = {"startbit", " length", "endianness", "signedness", "factor", "Offset" , "min" Max"};;
    order[signalname2] = vector<string> info = {"startbit", " length", "endianness", "signedness", "factor", "Offset" , "min" Max"};;
    order[signalname2] = vector<string> info = {"startbit", " length", "endianness", "signedness", "factor", "Offset" , "min" Max"};;

vector<integer> can1id = {"startbit", " length", "endianness", "signedness", "factor", "Offset" , "min" Max"};;