table 50100 "LibbscSharp Setup"
{
    DataClassification = ToBeClassified;

    fields
    {
        field(1; "Entry No."; Integer)
        {

        }
        field(10; "Azure Function URL"; Text[150])
        {
            DataClassification = ToBeClassified;
        }
        field(20; "Key"; Text[150])
        {

        }
    }


    keys
    {
        key(Key1; "Entry No.")
        {
            Clustered = true;
        }
    }
}