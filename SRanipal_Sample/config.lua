function sign(x)
    if x < 0 then
        return -1
    end
    return 1
end

function update(lipdata, eyedata)
    if eyedata ~= nil then -- If we have eye data
        -- Available data:
        --  eyedata
        --      combined
        --          convergenceDistance (number)
        --          convergenceDistanceValid (boolean)
        --          openness (number)
        --          gazeDirectionNormalized (xyz 3D vector)
        --          gazeOrigin (xyz 3D vector)
        --          pupilDiameter (number)
        --          pupilPosition (xy 2D vector)
        --          gazeOriginValid (boolean)
        --          gazeValid (boolean)
        --          pupilDiameterValid (boolean)
        --          opennessValid (boolean)
        --          pupilPositionInSensorAreaValid (boolean)
        --
        --      {left,right}
        --          gazeDirectionNormalized (xyz 3D vector)
        --          openness (number)
        --          gazeOrigin (xyz 3D vector)
        --          pupilDiameter (number)
        --          pupilPosition (xy 2D vector)
        --          gazeOriginValid (boolean)
        --          gazeValid (boolean)
        --          pupilDiameterValid (boolean)
        --          opennessValid (boolean)
        --          pupilPositionInSensorAreaValid (boolean)
        --          expression
        --              frown (number)
        --              squeeze (number)
        --              wide (number)

        -- I don't have an eye tracker, so this is just an example!
        if (eyedata.combined.gazeValid) then
            SendData("/avatar/parameters/EyeX", (-eyedata.combined.gazeDirectionNormalized.x+1)/2)
            SendData("/avatar/parameters/EyeY", (eyedata.combined.gazeDirectionNormalized.y+1)/2)
        end
        if (eyedata.left.opennessValid) then
            SendData("/avatar/parameters/EyeOpenLeft", eyedata.left.openness)
        end
        if (eyedata.right.opennessValid) then
            SendData("/avatar/parameters/EyeOpenRight", eyedata.right.openness)
        end
        eyeBrowLeft = (eyedata.left.expression.wide - eyedata.left.expression.frown + 1)/2
        eyeBrowRight = (eyedata.right.expression.wide - eyedata.right.expression.frown + 1)/2
        SendData("/avatar/parameters/EyebrowLeft", eyeBrowLeft)
        SendData("/avatar/parameters/EyebrowRight", eyeBrowRight)
    end
    if lipdata ~= nil then -- If we have lips data
        -- Available data:
        --  lipdata
        --      Jaw_Right (number)
        --      Jaw_Left (number)
        --      Jaw_Forward (number)
        --      Jaw_Open (number)
        --      Mouth_Ape_Shape (number)
        --      Mouth_Upper_Right (number)
        --      Mouth_Upper_Left (number)
        --      Mouth_Lower_Right (number)
        --      Mouth_Lower_Left (number)
        --      Mouth_Upper_Overturn (number)
        --      Mouth_Lower_Overturn (number)
        --      Mouth_Pout (number)
        --      Mouth_Smile_Right (number)
        --      Mouth_Smile_Left (number)
        --      Mouth_Sad_Right (number)
        --      Mouth_Sad_Left (number)
        --      Cheek_Puff_Right (number)
        --      Cheek_Puff_Left (number)
        --      Cheek_Suck (number)
        --      Mouth_Upper_UpRight (number)
        --      Mouth_Upper_UpLeft (number)
        --      Mouth_Lower_DownRight (number)
        --      Mouth_Lower_DownLeft (number)
        --      Mouth_Upper_Inside (number)
        --      Mouth_Lower_Inside (number)
        --      Mouth_Lower_Overlay (number)
        --      Tongue_LongStep1 (number)
        --      Tongue_LongStep2 (number)
        --      Tongue_Down (number)
        --      Tongue_Up (number)
        --      Tongue_Right (number)
        --      Tongue_Left (number)
        --      Tongue_Roll (number)
        --      Tongue_UpLeft_Morph (number)
        --      Tongue_UpRight_Morph (number)
        --      Tongue_DownLeft_Morph (number)
        --      Tongue_DownRight_Morph (number)

        -- This compression strategy was developed by [Vilar](https://github.com/Vilar24)! Thank you so much!
        tongueOutStart = lipdata["Tongue_LongStep1"]
        tongueOut = tongueOutStart + lipdata["Tongue_LongStep2"]
        jawOpen = lipdata["Jaw_Open"] - lipdata["Mouth_Ape_Shape"]
        jawOpen = jawOpen*1.3
        jawOpen = math.max(0, jawOpen)
        jawOpen = jawOpen^1.5
        jawOpen = jawOpen-(math.min(lipdata["Cheek_Puff_Left"], lipdata["Cheek_Puff_Right"]) * 1.5)
        mouthXsign = (lipdata["Jaw_Right"] - lipdata["Jaw_Left"])*0.5
        mouthXsign = mouthXsign + ((lipdata["Mouth_Lower_Right"] - lipdata["Mouth_Lower_Left"]) * 0.75)
        mouthXsign = mouthXsign + ((lipdata["Mouth_Upper_Right"] - lipdata["Mouth_Upper_Left"]) * 0.75)
        mouthX = math.abs(lipdata["Jaw_Right"] - lipdata["Jaw_Left"])
        mouthX = math.max(mouthX, math.abs(lipdata["Mouth_Lower_Right"] - lipdata["Mouth_Lower_Left"])) * 1.5
        mouthX = math.max(mouthX, math.abs(lipdata["Mouth_Upper_Right"] - lipdata["Mouth_Upper_Left"])) * 1.5
        mouthX = mouthX*sign(mouthXsign)
        frownPartialA = lipdata["Mouth_Lower_DownLeft"] * lipdata["Mouth_Lower_DownRight"]
        frownPartialB = (lipdata["Mouth_Lower_Overturn"] + lipdata["Mouth_Lower_Overlay"])*0.5
        frown = (lipdata["Mouth_Sad_Left"] + lipdata["Mouth_Sad_Right"]) * 0.5 + frownPartialA * 0.25 + frownPartialB * 0.25

        smileFrown = (lipdata["Mouth_Smile_Left"] + lipdata["Mouth_Smile_Right"]) * 0.5
        smileFrown = smileFrown - (frown * 0.5)
        bareTeeth = math.min(lipdata["Mouth_Upper_UpLeft"], lipdata["Mouth_Lower_DownLeft"])
        bareTeeth = bareTeeth+math.min(lipdata["Mouth_Upper_UpRight"], lipdata["Mouth_Lower_DownRight"])
        poutPartial = (lipdata["Mouth_Upper_Overturn"] * lipdata["Mouth_Lower_Overturn"] *
                    lipdata["Mouth_Upper_UpLeft"] *
                    lipdata["Mouth_Upper_UpRight"] *
                    lipdata["Mouth_Lower_DownLeft"] *
                    lipdata["Mouth_Lower_DownRight"])
        poutReal = lipdata["Mouth_Pout"]*2 + poutPartial * 2
        tongueOut = tongueOut-(poutReal*1.5)
        tongueOut = math.max(tongueOut,0)
        
        tongueUpDown = 0.5 + lipdata["Tongue_Up"]*0.5 - lipdata["Tongue_Down"]*0.5
        tongueRightLeft = 0.5 + lipdata["Tongue_Right"] * 0.5 - lipdata["Tongue_Left"] * 0.5

        SendData("/avatar/parameters/MouthX", (mouthX+1)/2)
        SendData("/avatar/parameters/JawOpen", (jawOpen+1)/2)
        SendData("/avatar/parameters/Smile", (smileFrown+1)/2)
        SendData("/avatar/parameters/Teeth", bareTeeth)
        SendData("/avatar/parameters/Pout", poutReal)
        SendData("/avatar/parameters/Tongue", tongueOut)
        SendData("/avatar/parameters/TongueX", tongueRightLeft)
        SendData("/avatar/parameters/TongueY", tongueUpDown)
    end
end