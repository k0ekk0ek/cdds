/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2015 PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */
package org.opensplice.config.meta;

public class MetaValueSize extends MetaValueNatural {
    public MetaValueSize(String doc, Long defaultValue, Long maxValue,
            Long minValue, String dimension) {
        super(doc, defaultValue, maxValue, minValue, dimension);
    }

    @Override
    public boolean setMaxValue(Object maxValue) {
        boolean result;

        if(maxValue instanceof Long){
            this.maxValue = maxValue;
            result = true;
        } else {
            result = false;
        }
        return result;
    }

    @Override
    public boolean setMinValue(Object minValue) {
        boolean result;

        if(minValue instanceof Long){
            this.minValue = minValue;
            result = true;
        } else {
            result = false;
        }
        return result;
    }

    @Override
    public boolean setDefaultValue(Object defaultValue) {
        boolean result;

        if(defaultValue instanceof Long){
            this.defaultValue = defaultValue;
            result = true;
        } else {
            result = false;
        }
        return result;
    }
}
